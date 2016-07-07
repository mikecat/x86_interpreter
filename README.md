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

* [coder32 edition | X86 Opcode and Instruction Reference 1.11](http://ref.x86asm.net/coder32.html)
* [Assembly Programming on x86-64 Linux (05)](http://www.mztn.org/lxasm64/amd05.html)
* [Assembly Programming on x86-64 Linux (04)](http://www.mztn.org/lxasm64/amd04.html)
* [Tips　IA32（x86）命令一覧　Jから始まる命令　Jcc命令](http://softwaretechnique.jp/OS_Development/Tips/IA32_Instructions/Jcc.html)
