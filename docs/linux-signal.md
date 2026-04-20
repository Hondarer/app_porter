# Linux シグナル仕様・制約

porter ライブラリの Linux 環境におけるシグナル取り扱いの仕様と、利用者に期待する振る舞いをまとめる。

## ライブラリのシグナルに対する基本方針

porter ライブラリはシグナルに干渉しない。

| 項目 | porter の振る舞い |
|------|-----------------|
| シグナルハンドラの登録 | 行わない |
| シグナルのブロック・マスク | 行わない |
| シグナルの送信 | 行わない |
| `SA_RESTART` / `sigprocmask` の操作 | 行わない |

ライブラリ内部のスレッド同期は POSIX シグナルではなく条件変数 (`pthread_cond_timedwait`) で実装されており、シグナルの配信に依存しない。これは Windows との共通実装を保つための設計による。

シグナルハンドラの登録・管理はすべて利用者の責務となる。

---

## SIGPIPE への対処

TCP 接続 (`POTR_TYPE_TCP` / `POTR_TYPE_TCP_BIDIR`) を使用している場合、相手が接続を切断した直後に porter 経由でデータを送信すると、OS が **SIGPIPE** を配信することがある。SIGPIPE のデフォルト動作はプロセス終了であり、porter はこれを抑制しない。

利用者は `potrOpenServiceFromConfig()` / `potrOpenService()` の前に以下のいずれかの対策を実施すること。

```c
/* 方法 A: プロセス全体で SIGPIPE を無視する */
signal(SIGPIPE, SIG_IGN);
```

```c
/* 方法 B: SIGPIPE を独自ハンドラで受け取る */
void sigpipe_handler(int sig) { (void)sig; /* 何もしない or ログ */ }
signal(SIGPIPE, sigpipe_handler);
```

方法 A が最も簡便。TCP の切断はエラーとして `potrSend()` の戻り値 (`POTR_ERROR`) で検知できるため、SIGPIPE によるプロセス終了を避けて戻り値ベースのエラーハンドリングに統一できる。

UDP のみ使用する場合 (`POTR_TYPE_UNICAST` / `POTR_TYPE_MULTICAST` / `POTR_TYPE_BROADCAST`) は SIGPIPE は発生しない。

---

## graceful shutdown の実装パターン

`potrCloseService()` は非同期シグナル安全ではない (内部でミューテックスを使用する)。**シグナルハンドラ内から `potrCloseService()` を呼んではならない**。

正しい終了シーケンスは以下の通り。

1. シグナルハンドラで `volatile sig_atomic_t` フラグを 0 にセットする
2. メインループがフラグを検出してループを抜ける
3. ループ脱出後に `potrCloseService()` を呼んでリソースを解放する

```c
static volatile sig_atomic_t g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
    /* ここで potrCloseService() を呼んではならない */
}

int main(void)
{
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    PotrHandle handle;
    potrOpenService(..., &handle);

    while (g_running)
    {
        /* メインループ処理 */
    }

    potrCloseService(handle);  /* シグナルハンドラの外で呼ぶ */
    return 0;
}
```

フラグ変数は `volatile sig_atomic_t` で宣言すること。`volatile` を省略するとコンパイラがレジスタ最適化でメモリへの書き込みを省略し、メインループがフラグの変化を検出できなくなる場合がある。

---

## シグナルハンドラの制約 (async-signal-safe)

シグナルハンドラはプロセス内の任意の箇所に割り込むため、ハンドラ内から呼べる関数は **async-signal-safe** なものに限定される。

| 関数 | 安全性 |
|------|--------|
| `volatile sig_atomic_t` への代入 | 安全 (アトミック保証) |
| `_exit()` | 安全 |
| `write()` | 安全 |
| `close()` | 安全 |
| `waitpid()` | 安全 |
| `potrCloseService()` などのライブラリ関数 | **非安全**(ミューテックスを含む) |
| `printf()` / `malloc()` / `free()` | **非安全**(内部でロックを取得) |

ハンドラ内では最小限の操作 (フラグのセット、安全なファイルディスクリプタ操作) のみを行い、後処理はメインループに委ねること。

---

## 内部スレッドへのシグナル配信

porter は `potrOpenService()` / `potrOpenServiceFromConfig()` の呼び出し時に内部スレッド (送信・受信・接続管理・ヘルスチェック) を生成する。

Linux では、プロセスに届くシグナルはシグナルをマスクしていないスレッドのいずれかに配信される。porter の内部スレッドはシグナルをマスクしないため、アプリケーションが意図したシグナル (SIGINT など) が内部スレッドに配信される可能性がある。

内部スレッドがシグナルを受け取った場合の影響:
- スレッドがブロッキング待機 (`recvfrom()` など) 中の場合、システムコールが `EINTR` で中断される
- porter の内部スレッドはこの EINTR を適切に処理する設計になっているため、通常は動作上の問題は生じない

ただし、シグナルをメインスレッドで確実に受け取りたい場合は、`potrOpenServiceFromConfig()` / `potrOpenService()` の前に `pthread_sigmask()` で内部スレッドに継承させたくないシグナルをマスクしておくこと。子スレッドは親スレッドのシグナルマスクを引き継ぐ。

```c
sigset_t mask;
sigemptyset(&mask);
sigaddset(&mask, SIGINT);
sigaddset(&mask, SIGTERM);

/* ここでマスクすると、以降に生成される内部スレッドは SIGINT/SIGTERM をマスクして生成される */
pthread_sigmask(SIG_BLOCK, &mask, NULL);

PotrHandle handle;
potrOpenService(..., &handle);

/* メインスレッドだけマスクを解除して受け取る */
pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
```

---

## 未ハンドルのシグナルへの注意

### デフォルト動作によるプロセス終了

以下のシグナルはデフォルト動作 (プロセス終了またはコアダンプ) のまま porter に届く可能性がある。用途に応じてアプリケーション側でハンドラを設定すること。

| シグナル | デフォルト動作 | 留意事項 |
|---------|--------------|---------|
| SIGPIPE | プロセス終了 | TCP 使用時は要対策 (上述) |
| SIGUSR1 / SIGUSR2 | プロセス終了 | 外部から `kill` コマンドで誤送信された場合に即終了する |
| SIGALRM | プロセス終了 | `alarm()` を使うサードパーティライブラリとの組み合わせで発生し得る |
| SIGHUP | プロセス終了 | 端末切断時に発生。デーモン化する場合は `SIG_IGN` か設定リロードに使用するのが一般的 |

### シグナルによるシステムコールの中断 (EINTR)

**シグナルが配信されると、そのスレッドでブロッキング状態にあるシステムコールは中断して -1 / `errno=EINTR` を返す。**
プロセス終了を引き起こさないシグナル (カスタムハンドラが登録済みのもの、または `SIG_IGN` に設定済みのもの) でも同様に中断が発生する。

porter の内部スレッドは `recvfrom()` / `sendto()` / `connect()` / `recv()` / `send()` などのブロッキングシステムコールを使用する。これらがシグナルによって EINTR で中断された場合でも、内部スレッドは EINTR を検出して呼び出しを再試行する設計になっており、通信処理は継続される。

利用者のコード (コールバック関数、メインループなど) でブロッキングシステムコールを使用している場合も、シグナルによる EINTR 中断が発生することに注意する。EINTR のハンドリングは利用者の責任である。

```c
/* ブロッキング read を使う場合の EINTR 対応例 */
ssize_t n;
do {
    n = read(fd, buf, sizeof(buf));
} while (n < 0 && errno == EINTR);
```

### SA_RESTART による自動再試行

`sigaction()` でシグナルハンドラを登録する際に `SA_RESTART` フラグを指定すると、そのシグナルによって中断されたシステムコールはカーネルが自動的に再試行する。利用者コードで EINTR を個別にハンドルする手間を省ける。

```c
struct sigaction sa;
sa.sa_handler = sig_handler;
sigemptyset(&sa.sa_mask);
sa.sa_flags = SA_RESTART;   /* 中断されたシステムコールを自動再試行 */
sigaction(SIGINT, &sa, NULL);
```

ただし、`SA_RESTART` がシステムコールの自動再試行を保証するのは一部の呼び出しに限られる。`pause()` や `select()` / `poll()` / `epoll_wait()` など待機系の呼び出しは `SA_RESTART` を指定しても EINTR を返すことがある (POSIX 規定の適用外)。これらを使用する場合は EINTR を明示的にハンドルすること。

porter 内部スレッドは `SA_RESTART` の有無にかかわらず EINTR を処理する実装になっているため、利用者が `SA_RESTART` を設定するかどうかは porter の動作に影響しない。
