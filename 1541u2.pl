#!/usr/bin/perl

#perl2exe_include utf8
#perl2exe_include PerlIO
#perl2exe_include PerlIO::scalar
#perl2exe_include "unicore/Heavy.pl"

use strict;
use IO::Socket::INET;

die "Syntax $0 <ip> <script>\n".
    "       $0 <ip> -c run:<filename>\n".
    "       $0 <ip> -c kernal:<filename>\n".
    "       $0 <ip> -c d64:<filename>\n".
    "       $0 <ip> -c rund64:<filename>\n".
    "       $0 <ip> -e <command> [-e <command> ...]\n" unless @ARGV >= 2;

my $ip = shift @ARGV;
my $escript = "";
while ($ARGV[0] eq "-e")
{
   shift @ARGV;
   $escript .= $ARGV[0]."\n";
   shift @ARGV;
}
if (!$escript && $ARGV[0] eq "-c")
{
   shift @ARGV;
   my $command = shift @ARGV;
   if (substr($command,0,4) eq "run:")
   {
      $escript = "load run from prg '".substr($command,4)."'\nsend\n";
   }
   elsif (substr($command,0,7) eq "kernal:")
   {
      $escript = "load at kernal 0 from bin '".substr($command,7)."'\nreset-c64\nsend\n";
   }
   elsif (substr($command,0,4) eq "d64:")
   {
      $escript = "insert-disk d64 '".substr($command,4)."'\nlarge-send\n";
   }
   elsif (substr($command,0,7) eq "rund64:")
   {
      $escript = "insert-disk run d64 '".substr($command,7)."'\nlarge-send\n";
   }
   else
   {
      die "Unknown short command\n";
   }
}

my $script = \$escript;
$script = shift @ARGV unless $escript;

die "Error parsing command line arguments" if @ARGV != 0;

my $file;
my $line;
my $packet = "";

my @commodoreCharset = (0) x 256;
$commodoreCharset[$_] = $_ for (0..255);
$commodoreCharset[64+$_] = 192+$_ for (1..26);
$commodoreCharset[96+$_] = 64+$_ for (1..26);

open ($file, "<", $script) or die "Cannot open $script\n";
while ($line = <$file>)
{
   chomp $line;
   next if $line =~ /^#/;
   next if $line =~ /^(\s*)$/;
   my @token = tokenizer($line);
   if ($token[0] eq "reset-c64" && @token == 1)
   {
      $packet .= "\x04\xFF\x00\x00";
   }
   elsif ($token[0] eq "send" && @token == 1)
   {
      die if length($packet) > 65534;
   
      my $socket = new IO::Socket::INET (
            PeerHost => $ip,
            PeerPort => 64,
            Proto => 'tcp',
        ) or die "Connection failed\n";
      $socket->send($packet);
      $socket->close();
      $packet = "";
   }
   elsif ($token[0] eq "large-send" && @token == 1)
   {
      die if length($packet) > 200000;
   
      my $socket = new IO::Socket::INET (
            PeerHost => $ip,
            PeerPort => 64,
            Proto => 'tcp',
        ) or die "Connection failed\n";
      $socket->send($packet);
      $socket->close();
      $packet = "";
   }
   elsif ($token[0] eq "debug" && @token == 1)
   {
      die if length($packet) > 65534;
      print unpack("H*", $packet)."\n";   
      $packet = "";
   }
   elsif ($token[0] eq "large-debug" && @token == 1)
   {
      die if length($packet) > 200000;
      print unpack("H*", $packet)."\n";   
      $packet = "";
   }
   elsif ($token[0] eq "wait" && @token == 2)
   {
      $packet .= "\x05\xFF" . pack("v", $token[1] );
   }
   elsif ($token[0] eq "keys" && @token > 1)
   {
      $packet .= "\x03\xFF" . pack("v", $#token ) . pack("c*", @token[1..$#token])  ;
   }
   elsif ($token[0] eq "load" && @token > 1)
   {
      my $bin = "";
      my $addr = "";
      my $reset = 0;
      my $run = 0;
      my $sizeprefix = 0;
      my $kernal = 0;
      my $test;
      my @token2 = @token;
      shift @token2;
      while (@token2)
      {
         if ($token2[0] eq "reset") { $reset = 1; shift @token2; }
         elsif ($token2[0] eq "run") { $run = 1; shift @token2; }
         elsif ($token2[0] eq "sizeprefix") { $sizeprefix = 1; shift @token2; }
         elsif ($token2[0] eq "at") 
	 {
	     shift @token2;
	     my $addr2 = shift @token2;
	     if ($addr2 eq "reu")
	     {
	        $addr2 = shift @token2;
		$addr = substr(pack("V", $addr2), 0, 3);
	     }
	     elsif ($addr2 eq "kernal")
	     {
	        $addr2 = shift @token2;
		$addr = pack("v", $addr2);
		$kernal = 1;
	     }

	     {
	        $addr = pack("v", $addr2);
	     }
	 }
         elsif ($token2[0] eq "values") 
	 {
	     shift @token2;
	     $bin = pack("c*", @token2);
	     @token2 = ();
	 }
         elsif ($token2[0] eq "from") 
	 {
	     shift @token2;
	     my $type = shift @token2;
	     my $infile = shift @token2;

             my $file2;
             open ($file2, "< :raw", $infile) or die "Cannot open $infile\n";
	     local $/;
	     undef $/;
	     my $tmp = <$file2>;
	     close $file2;
	     if ($type eq "prg")
	     {
	        $test = $tmp;
	        $addr = substr($tmp, 0, 2) if $addr eq "";
		$bin = substr($tmp, 2);
	     }
	     elsif ($type eq "bin")
	     {
	        $bin = $tmp;
	     }
	     else
	     {
	        die;
	     }
	 }
         elsif ($token2[0] eq "fill") 
	 {
	     shift @token2;
	     my $count = shift @token2;
	     my $value = shift @token2;
	     $bin = chr($value) x $count;
	 }
      
         else
	 {
	    die "Syntax error";
	 }
      }
      $bin = pack("V", length($bin)).$bin if $sizeprefix;
      die unless $addr ne "";
      my $prg = $addr.$bin;
      my $opcode = "\x06";
      $opcode = "\x01" if $reset;
      $opcode = "\x02" if $run;
      $opcode = "\x08" if $kernal;
      $opcode = "\x07" if length($addr) == 3;
      $packet .= "$opcode\xFF" . pack("v", length($prg) ) . $prg;
   }

   elsif ($token[0] eq "insert-disk" && @token > 1)
   {
      my $run = 0;
      my $d64 = 0;
      my $bin;
      my @token2 = @token;
      shift @token2;
      while (@token2)
      {
         if ($token2[0] eq "run") { $run = 1; shift @token2; }
         elsif ($token2[0] eq "d64") 
	 {
	     shift @token2;
	     my $infile = shift @token2;

             my $file2;
             open ($file2, "< :raw", $infile) or die "Cannot open $infile\n";
	     local $/;
	     undef $/;
	     $bin = <$file2>;
	     close $file2;
	     $d64 = 1;
	 }
         else
	 {
	    die "Syntax error";
	 }
      }
      die unless $d64;
      my $opcode = "\x0a";
      $opcode = "\x0b" if $run;
      $packet .= "$opcode\xFF" . substr(pack("V", length($bin)),0,3) . $bin;
   }



   elsif ($token[0] eq "reu-load-split" && @token > 1)
   {
      die unless $packet eq "";
   
      my $bin = "";
      my $addr = 0;
      my $reset = 0;
      my $run = 0;
      my $sizeprefix = 0;
      my $test;
      my @token2 = @token;
      shift @token2;
      while (@token2)
      {
         if ($token2[0] eq "sizeprefix") { $sizeprefix = 1; shift @token2; }
         elsif ($token2[0] eq "at") 
	 {
	     shift @token2;
	     $addr = shift @token2;
	 }
         elsif ($token2[0] eq "values") 
	 {
	     shift @token2;
	     $bin = pack("c*", @token2);
	     @token2 = ();
	 }
         elsif ($token2[0] eq "from") 
	 {
	     shift @token2;
	     my $type = shift @token2;
	     my $infile = shift @token2;

             my $file2;
             open ($file2, "< :raw", $infile) or die "Cannot open $infile\n";
	     local $/;
	     undef $/;
	     my $tmp = <$file2>;
	     close $file2;
	     if ($type eq "prg")
	     {
	        $test = $tmp;
		$bin = substr($tmp, 2);
	     }
	     elsif ($type eq "bin")
	     {
	        $bin = $tmp;
	     }
	     else
	     {
	        die;
	     }
	 }
         elsif ($token2[0] eq "fill") 
	 {
	     shift @token2;
	     my $count = shift @token2;
	     my $value = shift @token2;
	     $bin = chr($value) x $count;
	 }
      
         else
	 {
	    die "Syntax error" . @token2;
	 }
      }
      
      $bin = pack("V", length($bin)).$bin if $sizeprefix;
      
      while ($bin ne "")
      {
         my $bin2 = substr($bin, 0, 60*1024);
	 my $len2 = length($bin2);
	 $packet = "\x07\xFF" . pack("v", 3+$len2) . substr(pack("V", $addr),0,3) . $bin2; 
         my $socket = new IO::Socket::INET (
            PeerHost => $ip,
            PeerPort => 64,
            Proto => 'tcp',
           ) or die "Cannection failed\n";
         $socket->send($packet);
         $socket->close();
         $bin = substr($bin, $len2);
	 $addr += $len2;
      }
      
      $packet = "";

      ## $packet .= "$opcode\xFF" . pack("v", length($prg) ) . $prg;
   }
   else
   {
      die "Syntax error at ". join(" ", @token). "\n";
   }
}






sub tokenizer
{
   my $line = $_[0];
   
   my @ret = ();
   
   while ($line ne "")
   {
      if ($line =~ s/^ +//)
      {
      
      }
      elsif ($line =~ s/^[cC]=\"([^\"]+)\"//)
      {
         push (@ret, $commodoreCharset[ord($_)]) for split(//, $1);
      }
      elsif ($line =~ s/^([a-zA-Z][\-a-zA-Z0-9]*)//)
      {
         push (@ret, $1);
      }
      elsif ($line =~ s/^([0-9]+k)//)
      {
         push (@ret, $1*1024);
      }
      elsif ($line =~ s/^([0-9]+M)//)
      {
         push (@ret, $1*1024*1024);
      }
      elsif ($line =~ s/^([0-9]+)//)
      {
         push (@ret, $1);
      }
      elsif ($line =~ s/^(\$[0-9a-fA-F]+)//)
      {
         push (@ret, hex(substr($1,1)));
      }
      elsif ($line =~ s/^\"([^\"]+)\"//)
      {
         push (@ret, ord($_)) for split(//, $1);
      }
      elsif ($line =~ s/^\'([^\"]+)\'//)
      {
         push (@ret, $1);
      }
      else
      {
         die "Lexical Error at $line\n";
      }
   }
   
   @ret;
}
