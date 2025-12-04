# MarkovJunior

A C++ implementation of [MarkovJunior](https://github.com/mxgmn/MarkovJunior) using [StormKit](https://github.com/TapzCrew/StormKit)

## Getting started
### Setup
[Install xmake](https://xmake.io/#/getting_started).  

Configure xmake project and setup LLVM development kit.  

  I suppose it can work with LLVM v19 but I recommend using v20 at least. Developement is currently done using v21.  
  While developement is continuously made on `macosx` using the `--HEAD` version of homebrew package `llvm`, compilation is also sometimes tested for `linux` using the AUR package `llvm-git`.  
  Maybe it works with your default compiler toolchain, I can't guarantee anything but it's a good opportunity to open an issue !  

On `macosx`, we need to provide the path to swift-frontend for stormkit :
```sh
export PATH="/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin":$PATH
```

```sh
xmake f --toolchain=llvm \
        --sdk=<path_to_llvm> \
        --runtimes=c++_shared
```

Ask your package manager about `path_to_llvm` (`brew --prefix llvm`).  
| Plateform | path_to_llvm |
| --- | --- |
| `macosx` | `/usr/local/opt/llvm` |
| `linux`  | `/opt/llvm-git` |

Then build with `xmake build`. The executable can then be found at `build/<platform>/<arch>/<build_mode>/markovjunior`.  

### Execute

You can run the built executable with `xmake run markovjunior`.

You can provide a path to an xml model as first positional argument :  
```sh
markovjunior models/NestedGrowth.xml
```

You can enable the `--gui` flag to run graphics in a window instead of terminal.  
```sh
markovjunior --gui
```
