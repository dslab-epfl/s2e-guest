This directory contains a bunch of utilities needed to operate the
Chef symbolic execution platform for interpreted languages:

* ``build_llvm_i586.sh`` - A script used to fetch and build LLVM with
  Clang for the i586 architecture used in the Chef VMs.  Normally, you
  should not need to run this, as we already provide the pre-compiled
  binary packages.

* ``cmd_server.py`` - A small web server that listens for a command to
  execute inside the guest.  [This is a hack to be replaced by a
  solution that does not require network access in S2E mode.]

* ``llvm-pass`` - An LLVM basic block instrumentation pass used for
  fine-grained state merging in Chef.  You should read its own README
  file for more information.

