# push_to_github.ps1
# One-shot push of this project to your HOBBY GitHub account WITHOUT touching your
# global/work git config or caching the token in Windows Credential Manager.
#
# Run from anywhere:  powershell -ExecutionPolicy Bypass -File Tools\push_to_github.ps1
# It prompts for the repo URL, your hobby name/email, and the token (hidden input).
# The token is used only for this single push and is never written to disk or history.

$ErrorActionPreference = "Stop"

# Move to the repo root (this script lives in <repo>\Tools).
Set-Location -Path (Join-Path $PSScriptRoot "..")

$RepoUrl = Read-Host "Hobby repo HTTPS URL (e.g. https://github.com/user/repo.git)"
$Name    = Read-Host "Commit author name (hobby account)"
$Email   = Read-Host "Commit author email (hobby account)"

# Token entered at runtime, hidden, kept only in memory.
$sec   = Read-Host "Personal Access Token (input hidden)" -AsSecureString
$token = [System.Net.NetworkCredential]::new("", $sec).Password

# --- Local repo identity only: global config is untouched, so nothing to revert. ---
if (-not (Test-Path ".git")) { git init | Out-Null }
git branch -M main 2>$null
git config user.name  "$Name"      # LOCAL (this repo only) — not --global
git config user.email "$Email"

# --- Stage + commit (no-op if nothing changed). ---
git add -A
git commit -m "Update Kodo Tag V2" 2>$null | Out-Null

# --- Remote stays the CLEAN url (no token). ---
git remote remove origin 2>$null
git remote add origin "$RepoUrl"

# --- Push once with the token embedded in a temporary URL, with the credential helper
#     DISABLED so nothing is saved to Windows Credential Manager or .git/config. ---
$authUrl = $RepoUrl -replace '^https://', "https://x-access-token:$token@"
git -c credential.helper= push $authUrl main

# --- Point the branch at the clean origin for future pulls (no token stored). ---
git config branch.main.remote origin
git config branch.main.merge  refs/heads/main

# --- Wipe the token from memory. ---
$token = $null; $sec = $null; [System.GC]::Collect()

Write-Host ""
Write-Host "Done. Pushed to $RepoUrl as $Name <$Email>."
Write-Host "Global git config untouched; token not cached. Future pushes will prompt for the token again."
