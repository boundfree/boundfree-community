<?xml version="1.0" encoding="utf-8"?>
<TS version="2.1" language="zh_CN">
  <context>
    <name>app</name>
    <message><source>app.window_title</source><translation>{product_name} {version}</translation></message>
    <message><source>app.main.empty_state</source><translation>未打开文件</translation></message>
    <message><source>app.menu.file</source><translation>文件</translation></message>
    <message><source>app.menu.language</source><translation>语言</translation></message>
    <message><source>app.action.open</source><translation>打开文件</translation></message>
    <message><source>app.action.quit</source><translation>退出</translation></message>
    <message><source>app.dialog.open_title</source><translation>打开脑电文件</translation></message>
    <message><source>app.dialog.open_filter</source><translation>脑电文件 (*.csv *.h5 *.hdf5 *.edf *.bdf *.vhdr *.set *.fif);;所有文件 (*)</translation></message>
    <message><source>app.browser.previous</source><translation>上一窗口</translation></message>
    <message><source>app.browser.next</source><translation>下一窗口</translation></message>
    <message><source>app.browser.position</source><translation>起始</translation></message>
    <message><source>app.browser.window</source><translation>窗口</translation></message>
    <message><source>app.browser.amplitude</source><translation>幅值</translation></message>
    <message><source>app.browser.first_channel</source><translation>首通道</translation></message>
    <message><source>app.browser.channels</source><translation>通道</translation></message>
    <message><source>app.status.ready</source><translation>就绪</translation></message>
    <message><source>app.status.sidecar_connecting</source><translation>正在连接边车</translation></message>
    <message><source>app.status.sidecar_connected</source><translation>边车已连接</translation></message>
    <message><source>app.status.sidecar_failed</source><translation>边车连接失败：{error}</translation></message>
    <message><source>app.status.file_opening</source><translation>正在打开 {path}</translation></message>
    <message><source>app.status.file_failed</source><translation>打开文件失败：{error}</translation></message>
    <message><source>app.status.file_opened</source><translation>{format} | {fs} Hz | {n_channels} 通道</translation></message>
    <message><source>app.status.file_window</source><translation>{format} | {t0}-{t1} 秒 | +/-{amplitude} uV | {n_visible} 个可见通道</translation></message>
    <message><source>app.language.zh_CN</source><translation>简体中文</translation></message>
    <message><source>app.language.zh_TW</source><translation>繁体中文</translation></message>
    <message><source>app.language.en_US</source><translation>English</translation></message>
    <message><source>app.language.ja_JP</source><translation>日本語</translation></message>
    <message><source>app.language.ko_KR</source><translation>한국어</translation></message>
    <message><source>app.language.de_DE</source><translation>Deutsch</translation></message>
    <message><source>app.language.fr_FR</source><translation>Français</translation></message>
    <message><source>app.language.es_ES</source><translation>Español</translation></message>
    <message><source>app.language.pseudo</source><translation>伪本地化</translation></message>
    <message><source>error.protocol.invalid_json</source><translation>协议消息不是有效的 JSON</translation></message>
    <message><source>error.protocol.missing_field</source><translation>协议消息缺少字段：{field}</translation></message>
    <message><source>error.protocol.unknown_method</source><translation>未知协议方法：{method}</translation></message>
    <message><source>error.protocol.version_mismatch</source><translation>协议版本不一致，期望 {expected}，实际 {actual}</translation></message>
    <message><source>error.protocol.invalid_params</source><translation>协议参数无效：{field}，{reason}</translation></message>
    <message><source>error.file.not_found</source><translation>文件不存在：{path}</translation></message>
    <message><source>error.reader.unsupported_format</source><translation>暂不支持该文件格式：{path}，后缀 {suffix}</translation></message>
    <message><source>error.reader.open_failed</source><translation>读取器失败：{reason}</translation></message>
    <message><source>error.reader.session_not_found</source><translation>文件会话不存在：{handle}</translation></message>
    <message><source>error.reader.slice_out_of_range</source><translation>切片超出文件时长：{handle}，{t0}-{t1}，时长 {duration}</translation></message>
    <message><source>error.ipc.sidecar_start_failed</source><translation>无法启动边车：{reason}</translation></message>
    <message><source>error.ipc.sidecar_timeout</source><translation>边车操作超时：{operation}，{timeout_ms} ms</translation></message>
    <message><source>error.ipc.sidecar_exited</source><translation>边车异常退出，退出码：{exit_code}</translation></message>
    <message><source>error.sidecar.initialization_failed</source><translation>边车初始化失败：{reason}</translation></message>
    <message><source>error.license.missing</source><translation>缺少离线授权文件：{path}</translation></message>
    <message><source>error.license.invalid_signature</source><translation>离线授权签名无效：{reason}</translation></message>
    <message><source>error.license.expired</source><translation>离线授权已过期：{expires_at}</translation></message>
    <message><source>error.internal.unexpected</source><translation>内部错误：{operation}</translation></message>
  </context>
</TS>
