x86(80386)インタプリタ
======================

### これは何？
x86(80386)の命令(のサブセット)のインタプリタです。

### (暫定)仕様

* リアルモード・16ビットモードは簡単のため無し、32ビットのみ
* セグメント・制御レジスタなどは簡単のため無し
* もちろん(?)ページングも無し
* とりあえず浮動小数点演算も無し

### 参考資料

#### x86命令

* [coder32 edition | X86 Opcode and Instruction Reference 1.11](http://ref.x86asm.net/coder32.html)
* [Assembly Programming on x86-64 Linux (05)](http://www.mztn.org/lxasm64/amd05.html)
* [Assembly Programming on x86-64 Linux (04)](http://www.mztn.org/lxasm64/amd04.html)
* [Tips　IA32（x86）命令一覧　Jから始まる命令　Jcc命令](http://softwaretechnique.jp/OS_Development/Tips/IA32_Instructions/Jcc.html)
* [x86 and amd64 instruction reference](https://www.felixcloutier.com/x86/index.html)

#### ELFのファイルフォーマット

* [Tips　ELFフォーマットその１　ELFフォーマットについて](http://softwaretechnique.jp/OS_Development/Tips/ELF/elf01.html)
* [ELF Format](http://caspar.hazymoon.jp/OpenBSD/annex/elf.html)

#### PEファイルフォーマット

* [PE(Portable Executable)ファイルフォーマットの概要](http://home.a00.itscom.net/hatada/mcc/doc/pe.html)
* [アレ用の何か](http://hp.vector.co.jp/authors/VA050396/index.html)

#### xv6

* [6.828 / Fall 2018](https://pdos.csail.mit.edu/6.828/2018/xv6.html)
* [GitHub - mit-pdos/xv6-public: xv6 OS](https://github.com/mit-pdos/xv6-public)

#### PEファイル用ライブラリ

* [fflush(stdout) in c - Stack Overflow](https://stackoverflow.com/questions/4056026/fflushstdout-in-c)
  * `_iob`と`_flsbuf`の使い方がわかる
* [gettext - man pages section 3: Basic Library Functions](https://docs.oracle.com/cd/E36784_01/html/E36874/gettext-3c.html)
  * `libintl3.dll`系の関数の使い方がわかる
* [file handling - How feof() works in C - Stack Overflow](https://stackoverflow.com/questions/12337614/how-feof-works-in-c)
	* `_filbuf`の使い方がわかる
