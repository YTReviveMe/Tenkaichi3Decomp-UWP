$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$assets = Join-Path $PSScriptRoot 'assets'
$logoPath = Join-Path $assets 'LogoCutout.png'
$outputPath = Join-Path $assets 'SplashScreen.png'
$fontPath = Join-Path $PSScriptRoot '../third_party/fonts/fira/FiraSansCondensed-Bold.ttf'

$logo = [System.Drawing.Bitmap]::new($logoPath)
$splash = [System.Drawing.Bitmap]::new(620, 300, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$fonts = [System.Drawing.Text.PrivateFontCollection]::new()
$fonts.AddFontFile((Resolve-Path -LiteralPath $fontPath).Path)
$graphics = [System.Drawing.Graphics]::FromImage($splash)
$titleFont = [System.Drawing.Font]::new($fonts.Families[0], 41, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$decompFont = [System.Drawing.Font]::new($fonts.Families[0], 56, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$creditFont = [System.Drawing.Font]::new($fonts.Families[0], 20, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$white = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 244, 250, 255))
$blue = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 109, 193, 249))
$accent = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 50, 151, 221), 3)
try {
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

    $source = [System.Drawing.Rectangle]::new(38, 218, 1178, 820)
    $destination = [System.Drawing.Rectangle]::new(26, 55, 260, 190)
    $graphics.DrawImage($logo, $destination, $source, [System.Drawing.GraphicsUnit]::Pixel)
    $graphics.DrawString('TENKAICHI 3', $titleFont, $white, 287, 67)
    $graphics.DrawString('DECOMP', $decompFont, $white, 286, 111)
    $graphics.DrawLine($accent, 291, 184, 568, 184)
    $graphics.DrawString('UWP port by: ReviveMe', $creditFont, $blue, 290, 199)
    $splash.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $accent.Dispose()
    $blue.Dispose()
    $white.Dispose()
    $creditFont.Dispose()
    $decompFont.Dispose()
    $titleFont.Dispose()
    $graphics.Dispose()
    $fonts.Dispose()
    $splash.Dispose()
    $logo.Dispose()
}

Write-Output $outputPath
