<#
.SYNOPSIS
  Envia um JSON mock de telemetria para o endpoint do backend, simulando o POST
  que o firmware faz em enviar_dados_sensores().

.EXEMPLO
  .\enviar_mock.ps1
  .\enviar_mock.ps1 -Url "http://192.168.1.50:8000/api/telemetria" -Arquivo .\mock_envio.json
#>
param(
    [string]$Url = "http://192.168.1.50:8000/api/telemetria",
    [string]$Arquivo = "$PSScriptRoot\mock_envio.json"
)

$corpo = Get-Content -Path $Arquivo -Raw -Encoding UTF8

Write-Host "POST -> $Url" -ForegroundColor Cyan
try {
    $resposta = Invoke-RestMethod -Uri $Url -Method Post -Body $corpo `
        -ContentType "application/json" -TimeoutSec 5
    Write-Host "Sucesso. Resposta do servidor:" -ForegroundColor Green
    $resposta | ConvertTo-Json -Depth 10
}
catch {
    Write-Host "Falha no POST: $($_.Exception.Message)" -ForegroundColor Red
    if ($_.Exception.Response) {
        Write-Host "Status HTTP: $([int]$_.Exception.Response.StatusCode)" -ForegroundColor Red
    }
    exit 1
}
