# OpenAL Soft and HRTF audio

This guide describes an optional local audio setup for the rebuilt Windows client: the OpenAL Soft
`BF2OpenAL.dll` with HRTF rendering for headphones. HRTF and the direct-channels path below are
`[CHOSEN, not recovered]` policy, not retail behaviour.

## Which OpenAL the client loads

`OpenALProvider` (`Code/BF2/Sound/SoundEngine/OpenALProvider.cpp`) maps the Audio.con provider
and EAX setting to one of seven provider rows:

| Audio.con provider | EAX | Provider row | Library and device |
| --- | --- | --- | --- |
| `software` | on/off | Software Audio (+ EAX) | `BF2OpenAL.dll`, device `DirectSound` |
| `hardware` | on/off | Hardware Audio (+ EAX) | `BF2OpenAL.dll`, device `DirectSound3D` |
| `creative` | on/off | System OpenAL (+ EAX) | `OpenAL32.dll`, default device |

The Windows client packages ship OpenAL Soft as `BF2OpenAL.dll` beside `BF2.exe`
([OpenAL Soft in the Windows clients](ci-releases.md#openal-soft-in-the-windows-clients)).
`LoadLibrary` searches the executable's directory first, so that copy takes precedence over the
retail Creative wrapper in the game directory. OpenAL Soft treats the legacy device names
`DirectSound`, `DirectSound3D` and `MMSYSTEM` as its default device, and it implements the EAX
extensions, so the `software` and `hardware` rows both run on OpenAL Soft with EAX available.

Use **Software Audio + EAX** (`software`, EAX on; the default provider). `OpenAL32.dll` is not
needed: only `creative` loads it, and that row additionally requires an `EAX5.0` report unless
`AudioSettings.setAlwaysAllowSystemOpenAL 1` is set.

An empty or unknown provider name in Audio.con selects Software Audio when EAX is on (and fails
to apply when it is off), and the options menu then shows the untranslated key `AUDIOSETTINGS_`
because it localises `AUDIOSETTINGS_<provider>`. All test clients that share a profile also share
its Audio.con, so the client that saves last decides the provider of the next start.

## Setup

1. Put OpenAL Soft next to the rebuilt `BF2.exe` as `BF2OpenAL.dll`. A local CMake build does not
   stage it. Either run `Code/Build/build_openal_soft.py` with `--client-dir` (see
   [OpenAL Soft in the Windows clients](ci-releases.md#openal-soft-in-the-windows-clients)), or
   copy `soft_oal.dll` from the official 1.25.2 binary package (`bin\Win32` for the x86 client,
   `bin\Win64` for the x64 client) to `bin_client\BF2OpenAL.dll`. The architecture must match
   the client.
2. Put an `alsoft.ini` next to `BF2.exe`, or point `ALSOFT_CONF` at one:

   ```ini
   [general]
   channels = stereo
   stereo-mode = headphones
   stereo-encoding = hrtf
   ```

3. Select Software Audio with EAX in the audio options and restart the client.

OpenAL Soft also reads `%AppData%\alsoft.ini`; the file next to the executable is read after it
and overrides it.

### Why these three settings

OpenAL Soft renders HRTF only on stereo output. With its defaults (`channels` unset,
`stereo-mode = auto`, `stereo-encoding = basic`) it enables HRTF only when the Windows endpoint
reports the headphones form factor. An audio interface whose output is listed as speakers, such
as a USB interface with a headphone jack, gets plain stereo panning.

- `channels = stereo` forces a two-channel mix even when the endpoint is configured as quad or
  surround, so HRTF stays possible.
- `stereo-mode = headphones` tells OpenAL Soft the listener uses headphones whatever form factor
  the endpoint reports.
- `stereo-encoding = hrtf` selects HRTF as the stereo encoding. The older `hrtf = true` still
  works but is deprecated in favour of this key.

These are listener-side choices. On loudspeakers, HRTF output sounds coloured; remove the file
(or set `stereo-encoding = basic`) for speaker listening.

## Output device

The `software` and `hardware` rows map to OpenAL Soft's default device, so OpenAL Soft plays on
the Windows default playback device. Its WASAPI backend has no configuration option to choose
another device. Set the headphone output as the Windows default, or assign it to `BF2.exe` in the
Windows volume mixer.

## Diagnostics

Set `ALSOFT_LOGLEVEL=3` and `ALSOFT_LOGFILE=<path>` before starting the client. The log shows
the configuration files read, the device opened, the mix format and whether HRTF rendering was
enabled (for example `Full HRTF rendering enabled, using "Built-In HRTF"`). No log file means the
OpenAL Soft `BF2OpenAL.dll` was never loaded: the game directory's retail copy was used, or the
`creative` row was selected.

## Direct channels for music and 2D sources

Under HRTF, OpenAL Soft renders a multi-channel buffer as virtual speakers at ±30 degrees through
the HRTF filters, which makes stereo music and stereo 2D samples sound further away and coloured.
`[CHOSEN, not recovered]`, not in retail (issue pending): when the loaded OpenAL reports
`AL_SOFT_direct_channels`, `OpenALProvider::open` sets `g_directChannelsAvailable` and traces
`AL_SOFT_direct_channels available: Yes`. Then:

- the music thread sets `AL_DIRECT_CHANNELS_SOFT` on its source;
- `OpenALSample::start` sets it on 2D samples and clears it on 3D samples, because sources are
  pooled.

Music and stereo 2D sources play straight to the output channels, while 3D sources keep full HRTF
spatialisation. The property has no effect on mono buffers. The gate is the extension, not HRTF:
every provider row that loads OpenAL Soft, including the packaged default without HRTF, plays
music and stereo 2D sources this way. Without HRTF on stereo output the difference is small,
because the virtual ±30 degree speakers already land on the left and right channels. Creative's
retail `BF2OpenAL.dll` does not report the extension, so the retail provider path never receives
the property.
