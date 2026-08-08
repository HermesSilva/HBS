# Send one or more tools/call requests to a running gaitnet-mcp server and print
# the JSON results. Each -Call is "<tool> <json-arguments>" (arguments optional).
#
# Invoke it in-process (`& scripts\mcp_call.ps1 ...`), not via `powershell -File`,
# so that a multi-value -Call array is parsed as one parameter:
#
#   & scripts\mcp_call.ps1 -Call "describe_model"
#   & scripts\mcp_call.ps1 -Call 'load_atlas {"path":"data/atlas/arm26.osim"}',"describe_model"
param(
    [string[]]$Call,
    [int]$Port = 8766,
    [string]$Server = "127.0.0.1"
)

$ErrorActionPreference = "Stop"
$client = [System.Net.Sockets.TcpClient]::new($Server, $Port)
$stream = $client.GetStream()
$writer = [System.IO.StreamWriter]::new($stream); $writer.AutoFlush = $true
$reader = [System.IO.StreamReader]::new($stream)

$id = 0
foreach ($c in $Call) {
    $id++
    $tool, $rest = $c -split ' ', 2
    $toolArgs = if ($rest) { $rest } else { '{}' }
    $writer.WriteLine("{`"jsonrpc`":`"2.0`",`"id`":$id,`"method`":`"tools/call`",`"params`":{`"name`":`"$tool`",`"arguments`":$toolArgs}}")
    $line = $reader.ReadLine()
    Write-Output "### $tool"
    Write-Output $line
}
$client.Close()
