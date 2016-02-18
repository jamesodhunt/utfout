.. image:: https://img.shields.io/badge/license-GPL-3.0.svg

.. image:: https://travis-ci.org/jamesodhunt/utfout.svg?branch=master
   :target: https://travis-ci.org/jamesodhunt/utfout

.. image:: https://scan.coverity.com/projects/jamesodhunt-utfout/badge.svg
   :target: https://scan.coverity.com/projects/jamesodhunt-utfout
   :alt: Coverity Scan Build Status

.. image:: https://img.shields.io/badge/donate-flattr-blue.svg
   :alt: Donate via flattr
   :target: https://flattr.com/profile/jamesodhunt

.. image:: https://img.shields.io/badge/paypal-donate-blue.svg
   :alt: Donate via Paypal
   :target: https://www.paypal.me/jamesodhunt

======
utfout
======

.. contents::
.. sectnum::

Overview
--------

``utfout`` is a command-line tool that can produce UTF-8 (Unicode)
strings in various ways and direct them to standard output, standard
error or direct to the terminal without the need for shell support.

Strings can be repeated, delayed, randomly-generated, written to
arbitrary file descriptors, interspersed with other characters and
generated using ranges. ``printf(1)``-style escape sequences are supported
along with extended escape sequences. ``utfout(1)`` Sits somewhere between
``echo(1)`` and ``printf(1)``.

Examples
--------

Here are some interesting examples of usage::

  # Print "foofoofoo" to stderr, followed by "barbar" to stdout.
  utfout "foo" -r 2 -o "bar" -r 1
  
  # Write 50 nul bytes direct to the terminal.
  utfout -t "" -r 49
  
  # Write continuous stream of nul bytes direct to the terminal,
  # 1 per second.
  utfout -b 1s -t '' -r -1
  
  # Display a greeting slowly (as a human might type)
  utfout -b 20cs "Hello, $USER.\n"
  
  # Display a "spinner" that loops 4 times.
  utfout -b 20cs -p % "%r|%r/%r-%r\%r" -r 3
  
  # Display all digits between zero and nine with a trailing
  # newline.
  utfout "\{0..9}\n"
  
  # Display slowly the lower-case letters of the alphabet,
  # backwards without a newline.
  utfout -b 1ds "\{z..a}"
  
  # Display upper-case 'ABC' with newline.
  utfout '\u0041\u0042\u0043\n'
  
  # Display 'foo' with newline.
  utfout '\o146\u006f\x6F\n'
  
  # Clear the screen.
  utfout '\n' -r $LINES
  
  # Write hello to stdout, stderr and the terminal.
  utfout 'hello' -t -r 1 -e -r 1
  
  # Display upper-case letters of the alphabet using octal
  # notation, plus a newline.
  utfout "\{\o101..\o132}"
  
  # Display 'h.e.l.l.o.' followed by a newline.
  utfout -a . "hello" -a '' "\n"
  
  # Display upper-case and lower-case letters of the alphabet
  # including the characters in-between, with a trailing newline.
  utfout "\{A..z}\n"
  
  # Display lower-case alphabet followed by reversed lower-case alphabet
  # with the digits zero to nine, then nine to zero on the next line.
  utfout "\{a..z}\{z..a}\n\{0..9}\{9..0}\n"
  
  # Display lower-case Greek letters of the alphabet.
  utfout "\{..}"
  
  # Display cyrillic characters.
  utfout "\{..}"
  
  # Display all printable ASCII characters using hex range:
  utfout "\{\x21..\x7e}"
  
  # Display all printable ASCII characters using 2-byte UTF-8 range:
  utfout "\{\u0021..\u007e}"
  
  # Display all printable ASCII characters using 4-byte UTF-8 range:
  utfout "\{\U00000021..\U0000007e}"
  
  # Display all braille characters.
  utfout "\{\u2800..\u28FF}"
  
  # Display 'WARNING' in white on red background.
  utfout '\e[37;41mWARNING\e[0m\n'
  
  # Generate 10 random characters.
  utfout '\g' -r 9

Extended Example
----------------

It's not exactly curses, but here's a simple routine to draw a rectangle::

  $ cat >rectangle.sh<<EOT
  #!/bin/sh
  
  rectangle()
  {
      height="$1"
      width="$2"
      char="$3"
  
      r=$((width - 1))
      utfout "$char" -r $r '\n'
  
      for i in $(seq $((height - 2)))
      do
          utfout "$char" ' ' -r $((r - 2)) "$char\n"
      done
  
      utfout "$char" -r $r '\n'
  }
  
  [ $# -ne 3 ] && echo "ERROR: need height, width, and a character"
  rectangle "$1" "$2" "$3"
  EOT
  $ chmod 755 rectangle.sh
  $ ./rectangle.sh 10 20 ☻
  ☻☻☻☻☻☻☻☻☻☻☻☻☻☻☻☻☻☻☻☻
  ☻                  ☻
  ☻                  ☻
  ☻                  ☻
  ☻                  ☻
  ☻                  ☻
  ☻                  ☻
  ☻                  ☻
  ☻                  ☻
  ☻☻☻☻☻☻☻☻☻☻☻☻☻☻☻☻☻☻☻☻
  $

References
----------

See http://ifdeflinux.blogspot.co.uk/2012/09/out-output-utility.html

Author
------

``utfout`` was written by James Hunt <jamesodhunt@ubuntu.com>.
