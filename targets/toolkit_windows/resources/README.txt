Canoe Toolkit for Windows

Desktop GUI
Run canoe-boot-manager.bat from this directory to open the desktop GUI. The GUI requests Administrator at launch because writing the raw exported volume (`\\.\PhysicalDrive<N>`) requires elevation, matching comparable Windows raw-disk tools. The unsigned binary shows an unknown-publisher UAC prompt. The Windows package builds, but GUI runtime behavior is unverified here because there is no Windows machine or UAC under Wine. The CLI (`canoe.exe`) in this archive needs no WebView2 runtime; running it from an elevated prompt performs the same work, so you can use it instead if you will not approve the GUI prompt.
