param(
  [string]$ImagePath = "assets\skins\default.png",
  [int]$Size = 180,
  [int]$HostPid = 0
)

Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName WindowsBase

function Get-ParentProcessId {
  param([int]$ProcessId)
  $processInfo = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId" -ErrorAction SilentlyContinue
  if ($processInfo) {
    return [int]$processInfo.ParentProcessId
  }
  return 0
}

$mutexName = "Local\MiaCodePetOverlayWindow"
$mutex = New-Object System.Threading.Mutex($true, $mutexName)
if (-not $mutex.WaitOne(0, $false)) {
  return
}

$targetHostPid = $HostPid
if ($targetHostPid -le 0) {
  $parentPid = Get-ParentProcessId -ProcessId $PID
  $parentProcess = Get-Process -Id $parentPid -ErrorAction SilentlyContinue
  if ($parentProcess -and $parentProcess.ProcessName -eq "MiaCode") {
    $targetHostPid = $parentPid
  }
}

$signature = @"
using System;
using System.Runtime.InteropServices;

public static class Win32WindowRect {
  [StructLayout(LayoutKind.Sequential)]
  public struct RECT {
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
  }

  [DllImport("user32.dll")]
  public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
}
"@
Add-Type -TypeDefinition $signature

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$statePath = Join-Path $root "pet-window-state.json"

function Resolve-PetPath {
  param([string]$RelativePath)
  $candidate = Join-Path $root $RelativePath
  if (Test-Path -LiteralPath $candidate) {
    return $candidate
  }
  return $null
}

function New-PetBitmap {
  param([string]$ResolvedPath)
  $bitmap = New-Object System.Windows.Media.Imaging.BitmapImage
  $bitmap.BeginInit()
  $bitmap.CacheOption = [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad
  $bitmap.UriSource = New-Object System.Uri($ResolvedPath, [System.UriKind]::Absolute)
  $bitmap.EndInit()
  $bitmap.Freeze()
  return $bitmap
}

function Read-PetState {
  if (-not (Test-Path -LiteralPath $statePath)) {
    return @{}
  }
  try {
    return Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json
  } catch {
    return @{}
  }
}

function Write-PetState {
  param(
    [string]$Skin,
    [string]$HoverSkin
  )
  @{ skin = $Skin; hoverSkin = $HoverSkin } | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
}

function Get-SkinItems {
  $skinDir = Join-Path $root "assets\skins"
  if (-not (Test-Path -LiteralPath $skinDir)) {
    return @()
  }
  $supportedExtensions = '^\.(png|jpg|jpeg|gif|bmp|tif|tiff|ico)$'
  return Get-ChildItem -LiteralPath $skinDir -File |
    Where-Object { $_.Extension -match $supportedExtensions } |
    Sort-Object Name |
    ForEach-Object {
      [pscustomobject]@{
        Name = [System.IO.Path]::GetFileNameWithoutExtension($_.Name)
        RelativePath = "assets\skins\$($_.Name)"
      }
    }
}

function Get-DipScreenPoint {
  param(
    [System.Windows.Media.Visual]$Visual,
    [System.Windows.Point]$Point
  )
  $screenPoint = $Visual.PointToScreen($Point)
  $source = [System.Windows.PresentationSource]::FromVisual($Visual)
  if ($source -and $source.CompositionTarget) {
    return $source.CompositionTarget.TransformFromDevice.Transform($screenPoint)
  }
  return $screenPoint
}

$state = Read-PetState
if ($state.skin -and (Resolve-PetPath -RelativePath $state.skin)) {
  $ImagePath = $state.skin
}
$normalSkin = $ImagePath
$hoverSkin = ""
if ($state.hoverSkin -and (Resolve-PetPath -RelativePath $state.hoverSkin)) {
  $hoverSkin = $state.hoverSkin
}

$process = Get-Process -Name "MiaCode" -ErrorAction SilentlyContinue |
  Where-Object { $_.MainWindowHandle -ne 0 } |
  Select-Object -First 1

$screen = [System.Windows.SystemParameters]::WorkArea
$left = $screen.Left + $screen.Width - $Size - 60
$top = $screen.Top + $screen.Height - $Size - 80

$resolvedImage = Resolve-PetPath -RelativePath $ImagePath
if (-not $resolvedImage) {
  $ImagePath = "assets\skins\default.png"
  $resolvedImage = Resolve-PetPath -RelativePath $ImagePath
}
if (-not $resolvedImage) {
  return
}

if ($process) {
  $rect = New-Object Win32WindowRect+RECT
  if ([Win32WindowRect]::GetWindowRect($process.MainWindowHandle, [ref]$rect)) {
    $windowWidth = [Math]::Max(1, $rect.Right - $rect.Left)
    $windowHeight = [Math]::Max(1, $rect.Bottom - $rect.Top)
    $left = $rect.Left + [Math]::Round($windowWidth * 0.62) - $Size
    $top = $rect.Top + $windowHeight - $Size - 96
  }
}

$bitmap = New-PetBitmap -ResolvedPath $resolvedImage

$window = New-Object System.Windows.Window
$window.Width = $Size
$window.Height = $Size
$window.Left = $left
$window.Top = $top
$window.WindowStyle = [System.Windows.WindowStyle]::None
$window.ResizeMode = [System.Windows.ResizeMode]::NoResize
$window.AllowsTransparency = $true
$window.Background = [System.Windows.Media.Brushes]::Transparent
$window.Topmost = $true
$window.ShowInTaskbar = $false

$image = New-Object System.Windows.Controls.Image
$image.Source = $bitmap
$image.Width = $Size
$image.Height = $Size
$image.Stretch = [System.Windows.Media.Stretch]::Uniform
$image.RenderTransformOrigin = New-Object System.Windows.Point(0.5, 0.5)
$image.Cursor = [System.Windows.Input.Cursors]::Hand
$rotate = New-Object System.Windows.Media.RotateTransform(0)
$image.RenderTransform = $rotate

$window.Content = $image

function Set-PetSkin {
  param([string]$RelativePath)
  $nextPath = Resolve-PetPath -RelativePath $RelativePath
  if (-not $nextPath) {
    return
  }
  $script:normalSkin = $RelativePath
  $image.Source = New-PetBitmap -ResolvedPath $nextPath
  Write-PetState -Skin $script:normalSkin -HoverSkin $script:hoverSkin
}

function Set-HoverSkin {
  param([string]$RelativePath)
  if ($RelativePath -and -not (Resolve-PetPath -RelativePath $RelativePath)) {
    return
  }
  $script:hoverSkin = $RelativePath
  Write-PetState -Skin $script:normalSkin -HoverSkin $script:hoverSkin
}

function Show-PetSkin {
  param([string]$RelativePath)
  $nextPath = Resolve-PetPath -RelativePath $RelativePath
  if (-not $nextPath) {
    return
  }
  $image.Source = New-PetBitmap -ResolvedPath $nextPath
}

$skinMenu = New-Object System.Windows.Controls.MenuItem
$skinMenu.Header = "Skin"
$openSkinFolderItem = New-Object System.Windows.Controls.MenuItem
$openSkinFolderItem.Header = "Open Skin Folder"
$openSkinFolderItem.Add_Click({
  $skinDir = Join-Path $root "assets\skins"
  if (-not (Test-Path -LiteralPath $skinDir)) {
    New-Item -ItemType Directory -Force -Path $skinDir | Out-Null
  }
  Start-Process explorer.exe -ArgumentList $skinDir
})
$skinMenu.Items.Add($openSkinFolderItem) | Out-Null
$skinMenu.Items.Add((New-Object System.Windows.Controls.Separator)) | Out-Null
$skins = @(Get-SkinItems)
if ($skins.Count -eq 0) {
  $emptySkin = New-Object System.Windows.Controls.MenuItem
  $emptySkin.Header = "No skins"
  $emptySkin.IsEnabled = $false
  $skinMenu.Items.Add($emptySkin) | Out-Null
} else {
  foreach ($skin in $skins) {
    $skinItem = New-Object System.Windows.Controls.MenuItem
    $skinItem.Header = $skin.Name
    $skinItem.Tag = $skin.RelativePath
    $skinItem.Add_Click({
      param($sender, $eventArgs)
      Set-PetSkin -RelativePath ([string]$sender.Tag)
    })
    $skinMenu.Items.Add($skinItem) | Out-Null
  }
}

$hoverSkinMenu = New-Object System.Windows.Controls.MenuItem
$hoverSkinMenu.Header = "Hover Skin"
$openHoverSkinFolderItem = New-Object System.Windows.Controls.MenuItem
$openHoverSkinFolderItem.Header = "Open Skin Folder"
$openHoverSkinFolderItem.Add_Click({
  $skinDir = Join-Path $root "assets\skins"
  if (-not (Test-Path -LiteralPath $skinDir)) {
    New-Item -ItemType Directory -Force -Path $skinDir | Out-Null
  }
  Start-Process explorer.exe -ArgumentList $skinDir
})
$hoverSkinMenu.Items.Add($openHoverSkinFolderItem) | Out-Null
$hoverSkinMenu.Items.Add((New-Object System.Windows.Controls.Separator)) | Out-Null
$clearHoverSkinItem = New-Object System.Windows.Controls.MenuItem
$clearHoverSkinItem.Header = "None"
$clearHoverSkinItem.Add_Click({
  Set-HoverSkin -RelativePath ""
})
$hoverSkinMenu.Items.Add($clearHoverSkinItem) | Out-Null
$hoverSkinMenu.Items.Add((New-Object System.Windows.Controls.Separator)) | Out-Null
if ($skins.Count -eq 0) {
  $emptyHoverSkin = New-Object System.Windows.Controls.MenuItem
  $emptyHoverSkin.Header = "No skins"
  $emptyHoverSkin.IsEnabled = $false
  $hoverSkinMenu.Items.Add($emptyHoverSkin) | Out-Null
} else {
  foreach ($skin in $skins) {
    $hoverSkinItem = New-Object System.Windows.Controls.MenuItem
    $hoverSkinItem.Header = $skin.Name
    $hoverSkinItem.Tag = $skin.RelativePath
    $hoverSkinItem.Add_Click({
      param($sender, $eventArgs)
      Set-HoverSkin -RelativePath ([string]$sender.Tag)
    })
    $hoverSkinMenu.Items.Add($hoverSkinItem) | Out-Null
  }
}

$closeItem = New-Object System.Windows.Controls.MenuItem
$closeItem.Header = "Close"
$closeItem.Add_Click({ $window.Close() })

$contextMenu = New-Object System.Windows.Controls.ContextMenu
$contextMenu.Items.Add($skinMenu) | Out-Null
$contextMenu.Items.Add($hoverSkinMenu) | Out-Null
$contextMenu.Items.Add((New-Object System.Windows.Controls.Separator)) | Out-Null
$contextMenu.Items.Add($closeItem) | Out-Null
$image.ContextMenu = $contextMenu

$script:hostProcess = $null
if ($targetHostPid -gt 0) {
  try {
    $script:hostProcess = [System.Diagnostics.Process]::GetProcessById($targetHostPid)
    $script:hostProcess.EnableRaisingEvents = $true
    $script:hostProcess.add_Exited({
      $window.Dispatcher.BeginInvoke([Action]{
        $window.Close()
      }) | Out-Null
    })
  } catch {
    $window.Close()
  }
}

$script:isDragging = $false
$script:didMove = $false
$script:isMouseOver = $false
$script:dragStartScreen = $null
$script:dragStartLeft = 0
$script:dragStartTop = 0

function Start-PetSpin {
  if ($script:hoverSkin) {
    Show-PetSkin -RelativePath $script:hoverSkin
  } else {
    Show-PetSkin -RelativePath $script:normalSkin
  }
  $animation = New-Object System.Windows.Media.Animation.DoubleAnimation
  $animation.From = 0
  $animation.To = 360
  $animation.Duration = New-Object System.Windows.Duration([TimeSpan]::FromMilliseconds(650))
  $animation.FillBehavior = [System.Windows.Media.Animation.FillBehavior]::Stop
  $animation.Add_Completed({
    $rotate.Angle = 0
    if ($script:isMouseOver -and $script:hoverSkin) {
      Show-PetSkin -RelativePath $script:hoverSkin
    } else {
      Show-PetSkin -RelativePath $script:normalSkin
    }
  })
  $rotate.BeginAnimation([System.Windows.Media.RotateTransform]::AngleProperty, $animation)
}

$image.Add_MouseLeftButtonDown({
  param($sender, $eventArgs)
  $script:isDragging = $true
  $script:didMove = $false
  $script:dragStartScreen = Get-DipScreenPoint -Visual $sender -Point ($eventArgs.GetPosition($sender))
  $script:dragStartLeft = $window.Left
  $script:dragStartTop = $window.Top
  $sender.Cursor = [System.Windows.Input.Cursors]::SizeAll
  $sender.CaptureMouse() | Out-Null
  $eventArgs.Handled = $true
})

$image.Add_MouseMove({
  param($sender, $eventArgs)
  if (-not $script:isDragging) {
    return
  }
  $currentScreen = Get-DipScreenPoint -Visual $sender -Point ($eventArgs.GetPosition($sender))
  $dx = $currentScreen.X - $script:dragStartScreen.X
  $dy = $currentScreen.Y - $script:dragStartScreen.Y
  if ([Math]::Abs($dx) + [Math]::Abs($dy) -gt 3) {
    $script:didMove = $true
  }
  if ($script:didMove) {
    $window.Left = $script:dragStartLeft + $dx
    $window.Top = $script:dragStartTop + $dy
  }
  $eventArgs.Handled = $true
})

$image.Add_MouseLeftButtonUp({
  param($sender, $eventArgs)
  if (-not $script:isDragging) {
    return
  }
  $script:isDragging = $false
  $sender.ReleaseMouseCapture()
  $sender.Cursor = [System.Windows.Input.Cursors]::Hand
  if (-not $script:didMove) {
    Start-PetSpin
  }
  $eventArgs.Handled = $true
})

$image.Add_MouseEnter({
  param($sender, $eventArgs)
  $script:isMouseOver = $true
  if (-not $script:isDragging) {
    $sender.Cursor = [System.Windows.Input.Cursors]::Hand
    if ($script:hoverSkin) {
      Show-PetSkin -RelativePath $script:hoverSkin
    }
  }
})

$image.Add_MouseLeave({
  param($sender, $eventArgs)
  $script:isMouseOver = $false
  if (-not $script:isDragging) {
    $sender.Cursor = [System.Windows.Input.Cursors]::Arrow
    Show-PetSkin -RelativePath $script:normalSkin
  }
})

$window.Add_Closed({
  if ($script:hostProcess) {
    $script:hostProcess.Dispose()
  }
  $mutex.ReleaseMutex() | Out-Null
  $mutex.Dispose()
})

$window.ShowDialog() | Out-Null
