$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add('http://localhost:8000/')
try { $listener.Start() } catch { Write-Host "Порт 8000 занят"; exit 1 }
Write-Host "=== PowerShell HTTP-сервер на http://localhost:8000/ ==="
Write-Host "Ctrl+C для остановки"
while ($listener.IsListening) {
    $ctx = $listener.GetContext()
    $path = $ctx.Request.Url.LocalPath.TrimStart('/')
    $file = Join-Path $PWD $path
    Write-Host "Запрос: $path"
    if (Test-Path $file) {
        $ctx.Response.ContentType = 'text/plain; charset=utf-8'
        $bytes = [System.IO.File]::ReadAllBytes($file)
        $ctx.Response.OutputStream.Write($bytes, 0, $bytes.Length)
        $ctx.Response.StatusCode = 200
    } else {
        $ctx.Response.StatusCode = 404
    }
    $ctx.Response.Close()
}
