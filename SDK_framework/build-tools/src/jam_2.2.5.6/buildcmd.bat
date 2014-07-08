@echo off
::
:: Build JAM tool - assumes MSVC 7  ( Visual Studio 2003 .Net ) is installed
::

if "%VS71COMNTOOLS%" equ "" echo Visual Studio 2003 .Net does not seem to be installed && goto done


set INCLUDE=
set LIB=
call "%VS71COMNTOOLS%vsvars32.bat"

nmake -f Makefile NT

:done
exit /B
