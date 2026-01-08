#!/usr/bin/env perl

# SPDX-License-Identifier: AGPL-3.0-or-later

use strict;
use warnings;
use Getopt::Long;

# checkenv.pl - verify required build/runtime dependencies are available
# Usage: ./scripts/base/checkenv.pl [--verbose]

my $verbose = 0;
GetOptions('verbose|v' => \$verbose) or die "Invalid options\n";

my @deps = (
	{ name => 'make',             hints => 'install GNU make' },
	{ name => 'mtools',           hints => 'provides mcopy/mdir utilities' },
	{ name => 'mkfs.fat',         alt => 'mkfs.vfat', hints => 'mkfs.fat is provided by dosfstools on many systems' },
	{ name => 'nasm',             hints => 'Netwide Assembler' },
	{ name => 'i686-elf-as',      group => 'i686-elf-binutils', hints => 'part of i686-elf-binutils (assembler)' },
	{ name => 'i686-elf-ld',      group => 'i686-elf-binutils', hints => 'part of i686-elf-binutils (linker)' },
	{ name => 'i686-elf-gcc',     hints => 'cross-compiler for i686-elf' },
	{ name => 'qemu-system-i386', hints => 'QEMU i386 emulator' },
	{ name => 'perl',             hints => 'Perl interpreter (this script runs under perl)' },
);

my $missing = 0;

print "Checking dependencies...\n";

for my $dep (@deps) {
	my $found = find_executable($dep->{name});
	# -if an alternate name is provided, try that too
	if (!$found && $dep->{alt}) {
		$found = find_executable($dep->{alt});
	}

	if ($found) {
		print_status($dep->{name}, 'OK', $found);
		if ($verbose) {
			my $ver = probe_version($found);
			print "  version: " . ($ver // "(unknown)") . "\n" if defined $ver;
		}
	} else {
		$missing++;
		print_status($dep->{name}, 'MISSING');
		print "  hint: $dep->{hints}\n" if $dep->{hints};
	}
}

if ($missing) {
	print "\nMissing $missing dependencies.\n";
	print "Suggested actions: use your system package manager to install the missing programs.\n";
	print "Examples:\n  macOS (Homebrew): brew install nasm qemu mtools dosfstools gcc make\n";
	print "  Debian/Ubuntu: sudo apt install build-essential nasm qemu-system-x86 mtools dosfstools gcc make\n";
	print "For the cross toolchain (i686-elf-gcc / i686-elf-binutils) follow an OSDev cross-compiler guide:\n  https://wiki.osdev.org/GCC_Cross-Compiler\n";
	exit 2;
} else {
	print "\nAll dependencies found.\n";
	exit 0;
}

sub print_status {
	my ($name, $status, $path) = @_;
	printf "%-18s : %s", $name, $status;
	print " ($path)" if $path;
	print "\n";
}

sub find_executable {
	my ($prog) = @_;
	# -if the program path contains a slash, test it directly
	if ($prog =~ m{/}) {
		return $prog if -x $prog;
		return undef;
	}
	for my $dir (split /:/, $ENV{PATH} // '') {
		next unless length $dir;
		my $p = "$dir/$prog";
		return $p if -x $p;
	}
	return undef;
}

sub probe_version {
	my ($path) = @_;
	for my $flag ('--version', '-v', '-V', '-version') {
		my $out = qx{"$path" $flag 2>&1};
		next unless defined $out && length $out;
		$out =~ s/\s+$//;
		my ($line) = split /\n/, $out, 2;
		return $line if $line;
	}
	return undef;
}


