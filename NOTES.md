# Bugs (maybe)

Possible driver/platform bug replication and reporting.

Hardware:
 - AMD Radeon 9070 XT
 - NVidia RTX 4090

Compositors:
 - KDE Plasma (KWin),
 - Hyprland (aquamarine),
 - Niri (wlroots + xwayland-satellite)

## NVidia swapchain acquisition sync bug

1) `vkAcquireNextIMageKHR` on 3+ swapchains
2) `vkQueueSubmit` that waits on (2/3+) binary sempaphores at a time will lead to a deadlock after a number of submissions.
    - For 3 images, will deadlock after a number of 3 image waits (but works splitting waits into groups of 2)
    - For 4+ images, will deadlock after a number of 2+ image waits (so waits must be split into separate submissions)

## Niri/SDL3/Wayland resize acknowledge bug

1) Acquire surface from a resizable SDL3 window in Wayland mode.
2) Attempt to interactively resize in Niri
3) Niri will throttle resizes as it does not detect previous resizes as being handled

Notably, this is not observed when using a minimal raw libwayland test case, only when running through SDL3.
(other windowing libraries have not been tested, it's possible this is a driver/compositor bug that SDL has merely uncovered)

## Niri/SDL3/X11 incorrect initial size bug

1) Create multiple window in SDL3 in X11 mode, using the Niri compositor; where the windows are immediately tiled.
2) Call `SDL_GetWindowSizeInPixels`
3) All windows will show window sizes corresponding to their requested size, not the size the compositor tiles them to (This happens regardless of the number of windows created).
4) Only *one* window will receieve a subsequent resize eventto correct this disparity.
