# Development Reference

## Target workstation

```text
Creo root:
C:\Program Files\PTC\Creo 9.0.2.0

TOOLKIT includes:
C:\Program Files\PTC\Creo 9.0.2.0\Common Files\protoolkit\includes

TOOLKIT libraries:
C:\Program Files\PTC\Creo 9.0.2.0\Common Files\protoolkit\x86e_win64\obj

Unlock:
C:\Program Files\PTC\Creo 9.0.2.0\Parametric\bin\protk_unlock.bat
```

## Normal development command

```powershell
.\build-local.ps1
```

This configures, builds, verifies the DLL, and runs `protk_unlock.bat`.

Every rebuild creates a new binary and therefore the DLL must be unlocked again.

## Generate development protk.dat

```powershell
.\make-protk.ps1
```

Register the generated file in:

```text
Creo -> Tools -> Auxiliary Applications
```

## Resource directory

Native dialog resources must be available under:

```text
text\resource\aventics_toolbox.res
text\usascii\resource\aventics_toolbox.res
```

The application message file is:

```text
text\aventics_messages.txt
```

## Development reload

After build + unlock:

```text
Tools -> Auxiliary Applications
-> AventicsToolbox
-> Stop
-> Start
```

Restart Creo if the DLL remains locked by the process or if the native UI does not reload cleanly.
