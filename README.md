# kex

This project let you run [Kolibri OS][1] programms on your Linux host without installing Kolibri OS.

----------
How to compile
----

First of all you will need to install liblzma-dev:

    sudo apt-get install liblzma-dev

If you have installed [CodeLite][2] simply open project file and build it. Or enter followed command in terminal window:

    gcc -o kex main.c k_*.c -lX11 -lXrender -lrt -llzma -D_GNU_SOURCE

----------
How to use kex
----

Make sure you have placed followed files in your home folder:

 - ~/.kex/[char.mt][3]
 - ~/.kex/[charUni.mt][4]
 - ~/.kex/root/RD/1/DEFAULT.SKN

You can also mount kolibri.img in empty ~/.kex/root/RD/1 folder and [kolibri.iso][5] in ~/.kex/root/CD0/1 folder.

  [1]: http://www.kolibrios.org
  [2]: http://codelite.org
  [3]: http://websvn.kolibrios.org/dl.php?repname=Kolibri+OS&path=%2Fkernel%2Ftrunk%2Fgui%2Fchar.mt&rev=7211&peg=7211
  [4]: http://websvn.kolibrios.org/dl.php?repname=Kolibri+OS&path=%2Fkernel%2Ftrunk%2Fgui%2FcharUni.mt&rev=7211&peg=7211
  [5]: http://builds.kolibrios.org/rus/latest-iso.7z
