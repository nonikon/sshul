# ssh uploader

## Build
### Linux
Just run command `make` if libssh2 has already installed.  
Or run script `easybotton-build.sh` to build this project with build-in libssh2 (`CMake` needed).
### Windows
Download [mbedtls-2.16.11](https://github.com/ARMmbed/mbedtls/archive/refs/tags/mbedtls-2.16.11.tar.gz), [libssh2-1.9.0](https://github.com/libssh2/libssh2/archive/refs/tags/libssh2-1.9.0.tar.gz) source code and extract into this folder.   
Then, run `easybotton-build.cmd` (`CMake` and `MinGW` needed).

## Usage example
1. run command `sshul -t` to generate tempalte config file.
    ```json
    [{
        "remote_host": "192.168.1.1",
        "remote_port": 22,
        "remote_user": "root",
        "remote_passwd": "123456",
        "remote_path": "/tmp",
        "local_path": ".",
        "local_files": [
            "ma*.[ch]", "*/*.sh", ".vscode/**"
        ]
    }]
    ```
2. run command `sshul -l` to show the files to be uploaded.
    ```text
    main.c
    match.c
    match.h
    libssh2-1.9.0/ltmain.sh
    .vscode/settings.json
    ```
3. run command `sshul -u` to upload the files above to `remote_path` (/tmp).
4. run command `sshul -l` again, nothing will be shown. And `sshul -u` will upload nothing.
5. when some files are *modified*, `sshul -l` will show the *modified* files, and `sshul -u` will upload them.

## TODO
- Monitor files and upload automatically.