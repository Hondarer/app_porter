/**
 *******************************************************************************
 *  @file           porter_export.h
 *  @brief          porter の Windows DLL エクスポート/呼び出し規約マクロ。
 *  @author         Tetsuo Honda
 *  @date           2026/03/04
 *  @version        1.0.0
 *
 *  @copyright      Copyright (C) Tetsuo Honda. 2026. All rights reserved.
 *
 *******************************************************************************
 */

#ifndef PORTER_EXPORT_H
#define PORTER_EXPORT_H

#ifdef DOXYGEN

    /**
     *  @brief          DLL エクスポート/インポート制御マクロ。
     *
     *  ビルド条件に応じて以下の値を取ります。
     *
     *  | 条件                                                  | 値                       |
     *  | ----------------------------------------------------- | ------------------------ |
     *  | Linux (非 Windows)                                    | (空)                     |
     *  | Windows / `__INTELLISENSE__` 定義時                   | (空)                     |
     *  | Windows / `POTR_STATIC` 定義時 (静的リンク)           | (空)                     |
     *  | Windows / `POTR_EXPORTS` 定義時 (DLL ビルド)          | `__declspec(dllexport)`  |
     *  | Windows / `POTR_EXPORTS` 未定義時 (DLL 利用側)        | `__declspec(dllimport)`  |
     */
    #define POTR_EXPORT

    /**
     *  @brief          呼び出し規約マクロ。
     *
     *  Windows 環境では `__stdcall` 呼び出し規約を指定します。\n
     *  Linux (非 Windows) 環境では空に展開されます。\n
     *  すでに定義済みの場合は再定義されません。
     */
    #define POTR_API

#else /* !DOXYGEN */

    #ifndef POTR_STATIC
        #define POTR_STATIC 0
    #endif /* POTR_STATIC */
    #ifndef POTR_EXPORTS
        #define POTR_EXPORTS 0
    #endif /* POTR_EXPORTS */
    #include <com_util/base/dll_exports.h>
    #define POTR_EXPORT COM_UTIL_DLL_EXPORT(POTR)
    #define POTR_API    COM_UTIL_DLL_API(POTR)

#endif /* DOXYGEN */

#endif /* PORTER_EXPORT_H */
