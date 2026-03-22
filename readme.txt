
cd "C:\Users\User\Downloads\nice-main"

cl.exe /LD /EHsc /O2 proxy.cpp /link /out:version.dll





หา dump files : Run idle Client.exe
dumpbin /IMPORTS "C:\Users\User\Downloads\BambooRO_v6\BambooRO_v6\BamBoo_Client.exe" > imports.txt