#!/usr/bin/env python3
"""Offline A-1 regression test; never starts EpgTimerSrv or touches installed data.

Compile the CURRENT production synchronization body and its helpers verbatim.
The host object, clock, EPG search provider, and persistence are test doubles;
PreChgReserveData and debug logging are disabled (see README.md for limits).
LoadSetting and the INI/time utilities are linked from production sources.
Generated sources, binaries, and INI fixtures live in a temporary directory.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRV = ROOT / "EpgTimerSrv/EpgTimerSrv"


def between(text, first, last):
    if text.count(first) != 1 or text.count(last) != 1:
        raise RuntimeError("Production source boundaries changed; review the harness")
    return text[text.index(first):text.index(last)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutations", action="store_true")
    args = parser.parse_args()
    source = (SRV / "EpgTimerSrvMain.cpp").read_text(encoding="utf-8-sig")
    production = between(source, "struct AUTO_ADD_SYNC_WORK_ITEM {",
                         "void CtrlCmdResponseThreadCallback(")
    production += between(source, "bool CEpgTimerSrvMain::SyncChangeAutoAddReserveData(",
                          "void CEpgTimerSrvMain::SyncDeleteAutoAddReserveData(")
    mutations = {
        "fixed-60-seconds": ("cautionOnRecChange ? cautionOnRecMarginMin : 1", "1"),
        "old-reservation": ("CalcReserveStartTime(itr->second, defaultStartMargin)",
                            "CalcReserveStartTime(itrReserve->second, defaultStartMargin)"),
        "ignore-default-margin": (" : defaultStartMargin;", " : 0;"),
        "no-negative-clamp": ("std::max(startMargin, -duration)", "startMargin"),
        "inclusive-boundary": ("CalcReserveStartTime(itr->second, defaultStartMargin) > protectTime",
                               "CalcReserveStartTime(itr->second, defaultStartMargin) >= protectTime"),
        "drop-protected": ("++itr;", "itr = chgMap.erase(itr);"),
    }
    with tempfile.TemporaryDirectory(prefix="edcb-a1-") as tmp:
        work = Path(tmp)
        flags = ["g++", "-std=c++17", "-DNDEBUG", "-O1", "-g", "-pthread",
                 "-fsanitize=undefined", "-fno-sanitize-recover=all",
                 "-ffunction-sections", "-fdata-sections", "-I" + str(ROOT),
                 "-I" + str(SRV), "-I" + str(work)]
        objects = []
        for path in [SRV / "EpgTimerSrvSetting.cpp", ROOT / "Common/PathUtil.cpp",
                     ROOT / "Common/StringUtil.cpp", ROOT / "Common/TimeUtil.cpp"]:
            obj = work / (path.stem + ".o")
            subprocess.run(flags + ["-c", str(path), "-o", str(obj)], check=True)
            objects.append(str(obj))

        def run(body):
            (work / "production.inc").write_text(body, encoding="utf-8")
            binary = work / "test"
            subprocess.run(flags + [str(HERE / "test.cpp"), *objects,
                                    "-Wl,--gc-sections", "-ldl", "-o", str(binary)], check=True)
            return subprocess.run([str(binary), str(work / "fixture.ini")],
                                  text=True, capture_output=True)

        result = run(production)
        print(result.stdout, end="")
        if result.returncode:
            raise RuntimeError(result.stderr)
        if args.mutations:
            for name, (old, new) in mutations.items():
                if production.count(old) != 1:
                    raise RuntimeError("Mutation anchor changed: " + name)
                result = run(production.replace(old, new))
                if result.returncode != 1 or "FAIL:" not in result.stderr:
                    raise RuntimeError("Mutation not detected cleanly: " + name + result.stderr)
                failure = next(line for line in result.stderr.splitlines() if line.startswith("FAIL:"))
                print("Detected mutation:", name, failure)
            result = run(production)
            if result.returncode:
                raise RuntimeError(result.stderr)
            print("Final unmodified production run:", result.stdout.strip())


if __name__ == "__main__":
    main()
