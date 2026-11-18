# 构建与发布

产品功能见[首页](../README.md)，实现边界见[架构](../docs/architecture.md)。

## 本地构建

需要 Qt 6.11.2（Widgets、Sql、Network、Svg、Test）、MinGW 13.1、CMake 3.21+、Ninja、Git、Python 3.11+ 和 Windows PowerShell。客户端本身不依赖 Python；第三方源码已随仓库固定，见[依赖说明](third_party/README.md)。

从仓库根目录执行，按本机安装位置调整路径；Python 指向已安装的解释器：

```powershell
$qt = 'C:/Qt/6.11.2/mingw_64'
$mingw = 'C:/Qt/Tools/mingw1310_64'
$python = 'C:/Python313/python.exe'
$env:PATH = "$qt/bin;$mingw/bin;$env:PATH"
cmake -S app -B app/build-qt -G Ninja -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_PREFIX_PATH=$qt" "-DCMAKE_CXX_COMPILER=$mingw/bin/g++.exe" `
  "-DPython3_EXECUTABLE=$python"
cmake --build app/build-qt --parallel 3
ctest --test-dir app/build-qt --output-on-failure --no-tests=error
```

开发构建统一放在 `app/build-qt`；只构建客户端可加 `-DBUILD_TESTING=OFF`。测试开关、套件职责及隔离规则见[测试说明](tests/README.md)。从终端启动程序可复用上述 Qt 环境；独立部署使用 `app/deploy.ps1`。

## 发布

1. 更新 CMake 应用版本、`package.ps1` 默认标签及 `CHANGELOG.md` 对应条目。
2. 执行完整压力回归，提交全部源码及文档；工作树须干净。
3. 从仓库根目录打包，按安装位置覆盖 `QtRoot`、`MinGWRoot`、`CMakeExe`、`NinjaExe`、`QtSourceDir`：

   ```powershell
   ./app/package.ps1 -ReleaseTag v4.1.0
   python app/tests/verify_package.py 4.1.0 9
   ```

4. 确认 CI、包验收及必要的原生交互检查通过，为已验收提交打标签，发布 ZIP 与 `.zip.sha256`。Release 说明取自同版本的 `CHANGELOG.md` 条目。

打包从 `git archive HEAD` 全新构建，只安装应用、更新器、内置工具、运行库及许可证。ZIP 含 `build-info.json` 和逐文件校验清单；本机另存 `.build.json`。同名 ZIP 不覆盖，可用 `-OutputDir` 指定仓库内另一忽略目录。

成功后自动清理本次 `work-*`；失败保留现场，`-KeepWork` 可显式保留。`verify_package.py` 核对版本、源码提交、文件哈希、离线公式、无 SDK 环境的启动退出及数据库结构。

[Windows CI](../.github/workflows/windows-tests.yml) 在分支推送、PR 和手动触发时构建并运行压力回归；标签推送不重复触发。测试日志、截图、数据库、发布包和 `.impeccable/` 留在本地，不进入源码历史。
