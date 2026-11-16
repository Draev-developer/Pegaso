# Drags files, as Explorer does, from a small window of its own to a point of the screen.
#   powershell -STA -File dragdrop.ps1 <x> <y> <file> [<file> ...]
# The mouse is moved for about two seconds and then put back where it was.
param([int]$X, [int]$Y, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Files)

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -ReferencedAssemblies System.Windows.Forms, System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

public class DragSource : Form
{
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    const uint MOVE = 1, LEFTDOWN = 2, LEFTUP = 4;

    readonly string[] files;
    readonly Point target;
    public DragDropEffects Effect = DragDropEffects.None;

    public DragSource(string[] files, Point target)
    {
        this.files = files;
        this.target = target;
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.Manual;
        TopMost = true;
        ShowInTaskbar = false;
        Bounds = new Rectangle(target.X - 300, target.Y - 20, 40, 40);
        Shown += delegate
        {
            Point from = new Point(Left + 20, Top + 20);
            new Thread(delegate()
            {
                Thread.Sleep(300);
                SetCursorPos(from.X, from.Y);
                Thread.Sleep(100);
                mouse_event(LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
                for (int i = 1; i <= 20; ++i)
                {
                    Thread.Sleep(50);
                    SetCursorPos(from.X + (this.target.X - from.X) * i / 20, from.Y + (this.target.Y - from.Y) * i / 20);
                    mouse_event(MOVE, 0, 0, 0, UIntPtr.Zero);
                }
                Thread.Sleep(400);
                mouse_event(LEFTUP, 0, 0, 0, UIntPtr.Zero);
            }) { IsBackground = true }.Start();
        };
        MouseDown += delegate
        {
            Effect = DoDragDrop(new DataObject(DataFormats.FileDrop, this.files), DragDropEffects.Copy | DragDropEffects.Link);
            Close();
        };
    }
}
'@

$before = [System.Windows.Forms.Cursor]::Position
$form = New-Object DragSource -ArgumentList ([string[]]$Files), (New-Object System.Drawing.Point $X, $Y)
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 8000
$timer.add_Tick({ $form.Close() })
$timer.Start()
[System.Windows.Forms.Application]::Run($form)
[System.Windows.Forms.Cursor]::Position = $before
"effect: $($form.Effect)"
