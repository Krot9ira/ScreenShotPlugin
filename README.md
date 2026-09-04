# AsyncScreenShot

An Unreal Engine plugin for taking screenshots and render target captures without the frame hitch that
the built-in path costs.

Unreal's own screenshot request stalls the game thread on a full RHI flush, waits for the GPU, reads the
pixels back, and encodes the image — all inside one frame. On a 4K render target that is a visible hitch,
which rules it out for anything that captures during play: photo modes, replay tooling, automated
screenshot passes, thumbnail generation.

This plugin does the same work across several frames instead. The copy to a CPU-readable texture is
queued behind a GPU fence, the fence is polled once per frame, and the encode and the file write happen on
a background thread. A capture costs a few frames of latency and no measurable frame time.

## Requirements

- Unreal Engine 5.4 or newer
- Win64

The render target path uses nothing platform-specific and the Windows-only code is behind
`PLATFORM_WINDOWS`, but only Win64 is built and tested, so that is what the module allow-list says.

## Installing

Copy `Plugins/AsyncScreenShot` into your project's `Plugins` folder and rebuild. The repository is also a
working Unreal project. There is /Script/Engine.Blueprint'/Game/BP/ScreenCapturer.ScreenCapturer' that use plugin nodes to save screenshots of RT/Windows on "H" key press.

## Nodes

All nodes live under the **AsyncScreenShot** category.

### Save Render Target

The main one. Reads a `UTextureRenderTarget2D` back without stalling and writes it as PNG.

| Pin | Meaning |
| --- | --- |
| `Render Target` | Source. `PF_B8G8R8A8` and `PF_FloatRGBA` are supported. |
| `Path To Save`, `Name` | Destination folder and file name, no extension. Use forward slashes. |
| `Flush RHI` | Stalls for the result this frame. Defeats the point of the plugin; leave it off. |
| `Auto Unique Name` | Appends `_0001`, `_0002`, … instead of overwriting. |
| `Export HDR For Float RT` | Writes a Radiance `.hdr` with the raw linear values rather than a tonemapped PNG. |
| `Save To Disk` | Turn off to capture into memory only. |
| `Return As Texture` | Also hands back a transient `UTexture2D` on the completion pin. |
| `Crop X/Y/Width/Height` | Region to keep. Negative means no crop; a rectangle running off the edge is clamped. |
| `Downscale Factor` | Below 1 downscales (nearest neighbour). 1 or above is ignored — this never upscales. |
| **On Saved** | Fires with the path actually written and the texture, if one was requested. |
| **On Failed** | Fires when the readback or the write failed. The reason is in the log. |

`Full Path` matters: with `Auto Unique Name` on, the file that gets written is not the name you asked for.

### Capture Game Screen

Captures the game window through the OS, so the result includes everything on screen, UMG included —
which the render target path cannot see. PNG, JPG or BMP. Reports the written path, or fails.

`Save Game Screen` is the same capture as a plain fire-and-forget function, with no way to know whether it
worked. Prefer the node.

### Capture Super Resolution Screenshot

Renders the active player's view at a multiple of the viewport resolution through a temporary
`SceneCaptureComponent2D`, copying the player camera's FOV and post-process settings so the result matches
what is on screen. The multiplier is clamped to what the RHI can allocate — a 4× shot of a 4K viewport is
already a 530 MB render target.

### Save Render Targets Multiply Alpha

Writes a colour render target with its alpha multiplied by the inverse of a second render target's alpha,
for compositing a capture over another image. Both must be the same size.

### Helpers

- **Get Screenshot Save Path** — the project's default screenshot folder, absolute.
- **Set Png Compression Level** — zlib level 0–9. Applies to every subsequent PNG, not just the next one.
- **Save Screenshot Metadata** — writes a `Name.json` sidecar with arbitrary key/value pairs plus a
  timestamp. Useful for recording the camera transform or a build number next to a shot.

## How the readback works

1. `Activate()` enqueues a render command that copies the render target into a `CPUReadback` texture and
   writes a GPU fence.
2. A timer polls once per frame. Each tick enqueues a render command that checks the fence; until it
   signals, nothing happens.
3. Once it signals, the staging surface is mapped and converted to `FColor` (and to `FLinearColor` as well
   when an HDR export was asked for), honouring the surface pitch rather than assuming it equals the width.
4. The pixel buffer moves to a background thread, which encodes and writes it, then reports back on the
   game thread.

The readback gives up after 300 frames rather than polling forever if the fence never signals.

## Building and testing

```
Engine/Build/BatchFiles/Build.bat PluginMakerEditor Win64 Development -Project="<path>/PluginMaker.uproject"
```

The automation tests cover the parts that do not need a GPU — save paths, unique naming, the crop and
downscale maths, and the metadata sidecar:

```
UnrealEditor-Cmd.exe <path>/PluginMaker.uproject -ExecCmds="Automation RunTests AsyncScreenShot" -TestExit="Automation Test Queue Empty" -unattended -nullrhi
```

## Third-party

Image encoding uses [stb_image_write.h](https://github.com/nothings/stb) by Sean Barrett, vendored under
`Private/ThirdParty` and dual-licensed MIT / public domain.

## License

MIT — see [LICENSE](LICENSE).
