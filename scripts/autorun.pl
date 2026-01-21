#!/usr/bin/env perl
# SPDX-License-Identifier: BSD-3-Clause

use strict;
use warnings;
use Getopt::Long;

# Default configuration values
my %config = (
    'FLOPPY_IMAGE' => 'build/valkyrie_os.img',
    'DISK_IMAGE' => 'build/valkyrie_os.raw',
    'BOCHS_FLOPPY_CONFIG' => 'scripts/config/bochs_fda.txt',
    'BOCHS_DISK_CONFIG' => 'scripts/config/bochs_hda.txt',
    'GDB_FLOPPY_CONFIG' => 'scripts/config/gdb_fda.gdb',
    'GDB_DISK_CONFIG' => 'scripts/config/gdb_hda.gdb',
    'BUILD_COMMAND' => 'scons',
);

# Parse options
my $help = 0;
my $config_file = '';
GetOptions(
    'help|h' => \$help,
    'config=s' => \$config_file
) or usage();
usage() if $help;

# Load configuration from file if specified
if ($config_file && -f $config_file) {
    load_config($config_file);
}

# Build command configuration table using loaded config
my %commands = (
    # Auto-build and run commands (a*)
    'aqf' => { build => $config{BUILD_COMMAND}, run => ['qemu-system-i386', '-fda', $config{FLOPPY_IMAGE}] },
    'aqh' => { build => $config{BUILD_COMMAND}, run => ['qemu-system-i386', '-hda', $config{DISK_IMAGE}] },
    'abf' => { build => $config{BUILD_COMMAND}, run => ['bochs', '-f', $config{BOCHS_FLOPPY_CONFIG}] },
    'abh' => { build => $config{BUILD_COMMAND}, run => ['bochs', '-f', $config{BOCHS_DISK_CONFIG}] },
    'agf' => { build => $config{BUILD_COMMAND}, run => ['gdb', '-x', $config{GDB_FLOPPY_CONFIG}] },
    'agh' => { build => $config{BUILD_COMMAND}, run => ['gdb', '-x', $config{GDB_DISK_CONFIG}] },
    
    # Build-only commands (b-*)
    'b-f' => { build => $config{BUILD_COMMAND} . ' floppy_image', run => undef },
    'b-h' => { build => $config{BUILD_COMMAND} . ' disk_image', run => undef },
    
    # Run-only commands (r*)
    'rqf' => { build => undef, run => ['qemu-system-i386', '-fda', $config{FLOPPY_IMAGE}] },
    'rqh' => { build => undef, run => ['qemu-system-i386', '-hda', $config{DISK_IMAGE}] },
    'rbf' => { build => undef, run => ['bochs', '-f', $config{BOCHS_FLOPPY_CONFIG}] },
    'rbh' => { build => undef, run => ['bochs', '-f', $config{BOCHS_DISK_CONFIG}] },
    'rgf' => { build => undef, run => ['gdb', '-x', $config{GDB_FLOPPY_CONFIG}] },
    'rgh' => { build => undef, run => ['gdb', '-x', $config{GDB_DISK_CONFIG}] },
);

# Get command argument
my $cmd_arg = shift @ARGV;
unless (defined $cmd_arg) {
    warn "Error: No command specified!\n\n";
    usage();
}

# Look up command in table
my $cmd = $commands{$cmd_arg};
unless ($cmd) {
    warn "Error: Unknown command '$cmd_arg'!\n\n";
    usage();
}

# Execute build step if needed
if (defined $cmd->{build}) {
    print "Building: $cmd->{build}\n";
    my $result = system($cmd->{build});
    if ($result != 0) {
        die "Build failed with exit code $result\n";
    }
}

# Execute run step if needed
if (defined $cmd->{run}) {
    my @run_cmd = @{$cmd->{run}};
    print "Running: @run_cmd\n";
    exec @run_cmd or die "Failed to exec @run_cmd: $!\n";
}

exit 0;

sub load_config {
    my ($file) = @_;
    open my $fh, '<', $file or die "Failed to open config file '$file': $!\n";
    
    while (my $line = <$fh>) {
        # Skip comments and empty lines
        next if $line =~ /^\s*#/ || $line =~ /^\s*$/;
        
        # Parse KEY=VALUE pairs
        if ($line =~ /^\s*([A-Z_]+)\s*=\s*(.+?)\s*$/) {
            my ($key, $value) = ($1, $2);
            $config{$key} = $value;
        }
    }
    
    close $fh;
}

sub usage {
    print <<"USAGE";
Usage: $0 [OPTIONS] <command>

Commands:
  Auto-build and run:
    aqf     - Build and run in QEMU with floppy image
    aqh     - Build and run in QEMU with hard disk image
    abf     - Build and run in Bochs with floppy image
    abh     - Build and run in Bochs with hard disk image
    agf     - Build and debug with GDB (floppy)
    agh     - Build and debug with GDB (hard disk)
  
  Build only:
    b-f     - Build floppy image only
    b-h     - Build hard disk image only
  
  Run only (no build):
    rqf     - Run in QEMU with floppy image
    rqh     - Run in QEMU with hard disk image
    rbf     - Run in Bochs with floppy image
    rbh     - Run in Bochs with hard disk image
    rgf     - Debug with GDB (floppy)
    rgh     - Debug with GDB (hard disk)

Options:
  -h, --help          Show this help message
  --config <file>     Load configuration from file

Configuration:
  You can customize disk images and config paths in .automate file:
    FLOPPY_IMAGE          - Path to floppy disk image
    DISK_IMAGE            - Path to hard disk image
    BOCHS_FLOPPY_CONFIG   - Bochs config for floppy
    BOCHS_DISK_CONFIG     - Bochs config for hard disk
    GDB_FLOPPY_CONFIG     - GDB script for floppy
    GDB_DISK_CONFIG       - GDB script for hard disk
    BUILD_COMMAND         - Build command (default: scons)

Examples:
  $0 aqf                      # Build and run with QEMU (floppy)
  $0 --config .automate aqf   # Use custom config file
  $0 b-f                      # Just build floppy image
  $0 rqh                      # Run existing hard disk image in QEMU

USAGE
    exit 1;
}

1;

