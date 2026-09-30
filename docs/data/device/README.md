# Device performance logs

These files are one game session each, uploaded by an RG DS running ROCKNIXDS after the player agrees. The agreement is asked the first time the menu appears (A allows it, B does not) and can be changed later under Nintendo DS, *Share performance logs*.

A file is the same record as [`tools/rgds-monitor.py`](../../../tools/rgds-monitor.py): one JSON line a second (game file name, frames per second, dropped frames, CPU and GPU clocks and load, temperature, battery, and the new lines from `dsflip.log`) and a summary line when the game quits. `rgds-monitor.py report <file>` prints that summary.

The commits land on the `device-logs` branch, under `docs/data/device/<device-id>/`, so the beta branch the handheld tracks does not move every time a game quits. Tokens and passwords are stripped from the log lines. The handheld posts the file to an ntfy.sh queue (`rocknixds-perf-c4a91e7b2d08f653`); `.github/ingest-perf.py` commits it here. A token on the device is optional and commits directly.
