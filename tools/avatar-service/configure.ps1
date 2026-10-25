[CmdletBinding()]
param(
    [string]$Server = 'root@91.198.123.52',
    [string]$TokenFile = (Join-Path $env:LOCALAPPDATA 'vinoGram\avatar-service\bot-token.txt')
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $TokenFile)) {
    throw "Create a new bot with @BotFather and save its token in $TokenFile"
}
$botTokenValue = [IO.File]::ReadAllText($TokenFile).Trim()
if ($botTokenValue -notmatch '^[0-9]{5,}:[A-Za-z0-9_-]{20,}$') {
    throw 'The token file does not contain a valid bot token.'
}
$botEnvironment = [Convert]::ToBase64String(
    [Text.Encoding]::UTF8.GetBytes("VINOGRAM_BOT_TOKEN=$botTokenValue`n"))
$configureCommand = 'umask 077; python3 -c "import base64,sys,pathlib; p=pathlib.Path(''/etc/vinogram-avatars.env''); p.write_bytes(base64.b64decode(sys.stdin.read().strip(),validate=True)); p.chmod(0o600)" && systemctl restart vinogram-avatars && sleep 3 && curl --fail --silent http://127.0.0.1:8093/health'
$botEnvironment | & ssh -o BatchMode=yes -o ConnectTimeout=15 $Server $configureCommand
if ($LASTEXITCODE -ne 0) { throw 'Could not configure the avatar service.' }
Write-Output 'Only vinoGram avatar verification was restarted. Other projects were not changed.'
