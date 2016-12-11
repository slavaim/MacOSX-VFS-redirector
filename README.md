# MacOSX-VFS-redirector

##License

The license model is a BSD Open Source License. This is a non-viral license, only asking that if you use it, you acknowledge the authors, in this case Slava Imameev.

##Design

The filter is based on MacOSX-FileSystem-Filter. The filter design description can be found here https://github.com/slavaim/MacOSX-FileSystem-Filter .

The filter redirects file creation, open requests, rename and data IO (read, write) from an application to a shadow directory where shadow copies for files are created. The shadow directory path can cross mount points. An application under control doesn't aware about redirection and believes it works with original files by using unmodified paths. Applications under control are registered in gApplicationsData array. The array is declared in https://github.com/slavaim/MacOSX-VFS-redirector/blob/master/VFSFilter0/VFSFilter0/ApplicationsData.cpp .

The filter employs a user mode client for data modification and shadow file creation. See processing for VFSDataType_PreOperationCallback in https://github.com/slavaim/MacOSX-VFS-redirector/blob/master/VFSFilter0Client/VFSFilterClient/main.cpp .

The filter's core is https://github.com/slavaim/MacOSX-VFS-redirector/blob/master/VFSFilter0/VFSFilter0/VFSHooks.cpp . It contains VFS hooks to intercept file creation and open, redirect IO and call a user client.

The filter was tested on Mac OS X Yosemite (10.10) and Mac OS X El Capitan (10.11). A Sierra (10.12) support was added with https://github.com/slavaim/MacOSX-VFS-redirector/commit/48b7868d64b76b5da72bfce890180a0da323f028 commit. The vnode structure definition has changed in Sierra. A preprocessor condition in VersionDependent.cpp
```
#if !defined(MAC_OS_X_VERSION_10_11) || MAC_OS_X_VERSION_MIN_REQUIRED <= MAC_OS_X_VERSION_10_11
```
is used to choose the target OS version during compilation. If the condition is evaluated as true a kext will support Yosemite (10.10) and El Capitan (10.11), else a kext for Sierra (10.12) is being built. 

##Filter loading

The filter module is loaded by kextload command. The user client connects to the filter IOKit object to receive callbacks and modify data.

