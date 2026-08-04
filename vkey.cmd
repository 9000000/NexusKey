@echo off
REM Wrapper so `vkey local` just works.
REM
REM PowerShell's execution policy blocks unsigned scripts, and this repository
REM usually lives on a mapped WSL drive, which Windows treats as remote — so even
REM RemoteSigned refuses it. A .cmd file is not subject to the policy at all, and
REM it starts PowerShell with the policy bypassed for that one process only.
REM Nothing about the machine's configuration changes.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0vkey.ps1" %*
