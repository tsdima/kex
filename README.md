# kex

This project lets you to run [Kolibri OS][1] programs on your Linux host without installing Kolibri OS

----------
How to build
----

Install the build essential environment and LZMA library.

Ubuntu:

    sudo apt-get install ubuntu build-essential liblzma-dev

Fedora:

    sudo yum install liblzma-devel

or

    sudo yum install lzma-sdk457

Then use your gcc to build `kex` by entering the following command at your terminal window:

    gcc -o kex main.c k_*.c -lX11 -lXrender -lrt -llzma -ldl -D_GNU_SOURCE

Alternatively, if you have [CodeLite][2] you could simply add the project file into your existing (or new empty) workspace and build it

----------
How to setup
----

Create `~/.kex/` inside your home directory:

    mkdir ~/.kex/

Then you should place the following files to `~/.kex/`:

 - ~/.kex/[char.mt][3]
 - ~/.kex/[charUni.mt][4]
 
If you have `wget` - you could quickly wget them with the following commands:
 
    wget http://websvn.kolibrios.org/dl.php?repname=Kolibri+OS&path=%2Fkernel%2Ftrunk%2Fgui%2Fchar.mt
    wget http://websvn.kolibrios.org/dl.php?repname=Kolibri+OS&path=%2Fkernel%2Ftrunk%2Fgui%2FcharUni.mt
     
then
 
    mv ./char.mt ~/.kex/
    mv ./charUni.mt ~/.kex/
 
The only remaining file you need is
 
 - ~/.kex/root/RD/1/DEFAULT.SKN
 
Create the supporting directory tree:
 
    mkdir ~/.kex/
    mkdir ~/.kex/root/
    mkdir ~/.kex/root/RD/
    mkdir ~/.kex/root/RD/1/

Then you could obtain this DEFAULT.SKN file in the following way:
 
 1) download the [latest Kolibri OS floppy image][5] or just wget it:
 
     wget http://builds.kolibrios.org/eng/latest-img.7z

 2) extract it with 7zip if you have it installed:
 
     7za x latest-img.7z
 
 3) mount it to your empty `~/.kex/root/RD/1` directory:
 
    sudo mount -o loop kolibri.img ~/.kex/root/RD/1
     
Alternatively, you could get the latest [kolibri.iso][6] and mount it to `~/.kex/root/CD0/1` directory

----------
How to use
----

Run any KolibriOS program with `kex` !

Usage:

    $PATH_TO_KEX/kex kolibriapp args

Example:

    ~/kex-master/kex ~/.kex/root/RD/1/GAMES/XONIX

  [1]: http://www.kolibrios.org
  [2]: http://codelite.org
  [3]: http://websvn.kolibrios.org/dl.php?repname=Kolibri+OS&path=%2Fkernel%2Ftrunk%2Fgui%2Fchar.mt
  [4]: http://websvn.kolibrios.org/dl.php?repname=Kolibri+OS&path=%2Fkernel%2Ftrunk%2Fgui%2FcharUni.mt
  [5]: http://builds.kolibrios.org/eng/latest-img.7z
  [6]: http://builds.kolibrios.org/eng/latest-iso.7z
