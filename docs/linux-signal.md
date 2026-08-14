# Linux シグナル仕様・制約

porter ライブラリの Linux 環境におけるシグナル取り扱いの仕様と、利用者に期待する振る舞いをまとめます。

## ライブラリのシグナルに対する基本方針

porter ライブラリはシグナルに干渉しません。

| 項目 | porter の振る舞い |
|------|-----------------|
| シグナル ハンドラーの登録 | 行わない |
| シグナルのブロック・マスク | 行わない |
| シグナルの送信 | 行わない |
| `SA_RESTART` / `sigprocmask` の操作 | 行わない |

ライブラリ内部のスレッド同期は POSIX シグナルではなく条件変数 (`com_util_condvar_wait()` の timeout 指定) で実装されており、シグナルの配信に依存しません。これは Windows との共通実装を保つための設計によります。

利用者自身が扱うシグナルのハンドラー登録と管理は、利用者の責務となります。  
ただし、porter 内部の TCP 送信によって発生し得る SIGPIPE は、プロセス全体のシグナル設定を変更せずに送信単位で抑制します。

---

## SIGPIPE の抑制

TCP 接続 (`POTR_TYPE_TCP` / `POTR_TYPE_TCP_BIDIR`) で相手が接続を切断した直後にデータを送信しても、porter は SIGPIPE を利用者へ配信しません。  
内部の TCP 送信は Linux の `MSG_NOSIGNAL` を使用し、SIGPIPE を送信単位で抑制します。

SIGPIPE を抑制しても送信エラーは破棄しません。  
porter は送信エラーを既存の接続状態と結果コードの処理へ渡します。`potr_send()` のキュー登録型の戻り値契約は変わりません。

porter は利用者が登録した SIGPIPE ハンドラーやシグナル マスクを変更しません。  
porter の外で利用者が直接 `send()` や `write()` を呼び出す場合の SIGPIPE 対策は、利用者の責務です。

UDP (`POTR_TYPE_UNICAST` / `POTR_TYPE_MULTICAST` / `POTR_TYPE_BROADCAST`) の送信は、この抑制の対象外です。

---

## graceful shutdown の実装パターン

`potr_service_close()` は非同期シグナル安全ではありません (内部でミューテックスを使用します)。**シグナル ハンドラー内から `potr_service_close()` を呼んではなりません**。

正しい終了シーケンスは以下のとおりです。

1. シグナル ハンドラーで `volatile sig_atomic_t` フラグを 0 にセットします。
2. メイン ループがフラグを検出してループを抜ける
3. ループ脱出後に `potr_service_close()` を呼んでリソースを解放します。

```c
static volatile sig_atomic_t s_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    s_running = 0;
    /* ここで potr_service_close() を呼んではならない */
}

int main(void)
{
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    potr_context *handle;
    potr_service_open(..., &handle);

    while (s_running)
    {
        /* メイン ループ処理 */
    }

    potr_service_close(handle);  /* シグナル ハンドラーの外で呼ぶ */
    return 0;
}
```

フラグ変数は `volatile sig_atomic_t` で宣言してください。`volatile` を省略するとコンパイラがレジスター最適化でメモリへの書き込みを省略し、メイン ループがフラグの変化を検出できなくなる場合があります。

---

## シグナル ハンドラーの制約 (async-signal-safe)

シグナル ハンドラーはプロセス内の任意の箇所に割り込むため、ハンドラー内から呼べる関数は **async-signal-safe** なものに限定されます。

| 関数 | 安全性 |
|------|--------|
| `volatile sig_atomic_t` への代入 | 安全 (アトミック保証) |
| `_exit()` | 安全 |
| `write()` | 安全 |
| `close()` | 安全 |
| `waitpid()` | 安全 |
| `potr_service_close()` などのライブラリ関数 | **非安全** (ミューテックスを含む) |
| `printf()` / `malloc()` / `free()` | **非安全** (内部でロックを取得) |

ハンドラー内では最小限の操作 (フラグのセット、安全なファイル記述子操作) のみを行い、後処理はメイン ループに委ねてください。

---

## 内部スレッドへのシグナル配信

porter は `potr_service_open()` / `potr_service_open_from_config()` の呼び出し時に内部スレッド (送信・受信・接続管理・ヘルスチェック) を生成します。

Linux では、プロセスに届くシグナルはシグナルをマスクしていないスレッドのいずれかに配信されます。porter の内部スレッドはシグナルをマスクしないため、アプリケーションが意図したシグナル (SIGINT など) が内部スレッドに配信される可能性があります。

内部スレッドがシグナルを受け取っても、porter の動作には影響しません。  
内部スレッドは `com_util` の `net` カテゴリを経由してブロッキング システム コールを発行しており、シグナルによる中断は `com_util` が吸収するためです。詳細は [シグナルによるシステム コールの中断 (EINTR)](#シグナルによるシステム-コールの中断-eintr) を参照してください。

ただし、シグナルをメイン スレッドで確実に受け取りたい場合は、`potr_service_open_from_config()` / `potr_service_open()` の前に `pthread_sigmask()` で内部スレッドに継承させたくないシグナルをマスクしておいてください。子スレッドは親スレッドのシグナル マスクを引き継ぎます。

```c
sigset_t mask;
sigemptyset(&mask);
sigaddset(&mask, SIGINT);
sigaddset(&mask, SIGTERM);

/* ここでマスクすると、以降に生成される内部スレッドは SIGINT/SIGTERM をマスクして生成される */
pthread_sigmask(SIG_BLOCK, &mask, NULL);

potr_context *handle;
potr_service_open(..., &handle);

/* メイン スレッドだけマスクを解除して受け取る */
pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
```

---

## 未ハンドルのシグナルへの注意

### デフォルト動作によるプロセス終了

以下のシグナルはデフォルト動作 (プロセス終了またはコア ダンプ) のまま porter に届く可能性があります。用途に応じてアプリケーション側でハンドラーを設定してください。

| シグナル | デフォルト動作 | 留意事項 |
|---------|--------------|---------|
| SIGPIPE | プロセス終了 | porter 内部の TCP 送信では抑制します。porter 外の送信は利用者が対処します。 |
| SIGUSR1 / SIGUSR2 | プロセス終了 | 外部から `kill` コマンドで誤送信された場合に即終了します。 |
| SIGALRM | プロセス終了 | `alarm()` を使うサード パーティー ライブラリとの組み合わせで発生し得ます。 |
| SIGHUP | プロセス終了 | 端末切断時に発生。デーモン化する場合は `SIG_IGN` か設定リロードに使用するのが一般的 |

### シグナルによるシステム コールの中断 (EINTR)

porter の内部スレッドは `com_util` の `net` カテゴリを経由してブロッキング システム コールを発行します。  
`com_util` の公開 API がシグナルによる中断を吸収するため、porter の利用者は EINTR を意識する必要がありません。  
`COM_UTIL_CAUSE_INTERRUPTED` を判定して再試行する処理は不要であり、porter の戻り値がシグナルの配信を理由に失敗となることもありません。

分類ごとの規範と、その根拠は [com_util のコーディング規範](../../com_util/docs/coding-guideline.md) の「シグナル割り込み (EINTR) の扱い」に定めています。本書では繰り返しません。

Linux では一般に、シグナルが配信されると、そのスレッドでブロッキング状態にあるシステム コールが中断して -1 / `errno=EINTR` を返す場合があります。  
プロセス終了を引き起こさないシグナルでも同様です。

この一般則は、利用者のコード (コールバック関数、メイン ループなど) が OS のブロッキング システム コールを直接使用する場合に適用されます。  
この範囲の EINTR の処理は利用者の責任です。

```c
/* ブロッキング read を使う場合の EINTR 対応例 */
ssize_t n;
do {
    n = read(fd, buf, sizeof(buf));
} while (n < 0 && errno == EINTR);
```

### SA_RESTART による自動再試行

`sigaction()` でシグナル ハンドラーを登録する際に `SA_RESTART` フラグを指定すると、そのシグナルによって中断されたシステム コールはカーネルが自動的に再試行します。利用者コードで EINTR を個別にハンドルする手間を省けます。

```c
struct sigaction sa;
sa.sa_handler = sig_handler;
sigemptyset(&sa.sa_mask);
sa.sa_flags = SA_RESTART;   /* 中断されたシステム コールを自動再試行 */
sigaction(SIGINT, &sa, NULL);
```

ただし、`SA_RESTART` がシステム コールの自動再試行を保証するのは一部の呼び出しに限られます。`pause()` や `select()` / `poll()` / `epoll_wait()` など待機系の呼び出しは `SA_RESTART` を指定しても EINTR を返します。これらを直接使用する場合は EINTR を明示的にハンドルしてください。

porter と `com_util` は `SA_RESTART` の有無にかかわらず EINTR を処理する実装であるため、利用者が `SA_RESTART` を設定するかどうかは porter の動作に影響しません。  
`SA_RESTART` の設定は、利用者コードが直接発行するシステム コールにのみ影響します。

> [!NOTE]
> `com_util` が `SA_RESTART` に依存しない理由は、上記のとおり待機系の呼び出しが `SA_RESTART` では再開されないことと、ライブラリが利用者のハンドラー設定を制御できないことによります。
