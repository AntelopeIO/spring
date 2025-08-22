#!/usr/bin/perl
# An apt method that validates InRelease files are a configured hash value. Useful to ensure the package repo is exactly what is expected.
use strict;
use warnings;
use IPC::Open2;
use feature 'signatures';
no warnings 'experimental::signatures';

$| = 1; # disable output buffering

my %expected_hashes;
my ($http_child_out, $http_child_in);
my $pid = open2($http_child_out, $http_child_in, "/usr/lib/apt/methods/http");

# get the capabilities from the http method and forward it on to apt unchanged
print STDOUT read_block($http_child_out);

# read the configuration from apt and forward it to http method unchanged, but also inspect for our configuration
my @config_block = read_block(*STDIN);
if (@config_block && $config_block[0] =~ /^601 Configuration/) {
   for my $line (@config_block) {
      if ($line =~ /^Config-Item: ?Acquire::pinned::InReleaseHashes::([^=]+)=([^\s]+)/) {
         my ($key, $hash) = ($1, $2);
         $expected_hashes{$key} = $hash;
      }
   }
   print $http_child_in @config_block;
} else {
   die "did not receive 601 Configuration message from apt as expected.\n";
}

#main loop: read a block from apt with the command to run
while (my @command_block = read_block(*STDIN)) {
   my $original_uri_from_apt = parse_block(@command_block)->{URI} // die "failed to get URI from command block\n";
   #perform some filtering on the headers before forwarding on to http method
   @command_block = map { s/(URI: ?)pinned:/$1http:/r }      # replace URI: pinned://... with URI: http://...
                    grep { !/^Last-Modified:/ }              # scrub Last-Modified because cache hits will not have a file to check hash against
                    @command_block;

   print $http_child_in @command_block;

   #make note if we're getting an InRelease file and the expected hash of it. We need to track this now because we may encounter a redirect
   # to a very opaque filename
   my $expected_hash;
   if($original_uri_from_apt =~ /InRelease$/) {
      my ($inrelease_suite) = $original_uri_from_apt =~ m{/([^/]+)/InRelease$} or die "\nInRelease without a suite match\n";
      unless (exists $expected_hashes{$inrelease_suite}) {
         die "Unconfigured hash for suite '$inrelease_suite'\n";
      }
      $expected_hash = $expected_hashes{$inrelease_suite};
   }
   elsif($original_uri_from_apt =~ /Release$/) {  #just paranoia that apt doesn't fetch from this unchecked Release file instead
      die "Got $original_uri_from_apt which is not expected";
   }

   #read back all response blocks from http method until seeing a result code that is the end of the request
   while (my @child_response_block = read_block($http_child_out)) {
      my $child_response_info = parse_block(@child_response_block);

      #annoyingly, on buster, the http method won't handle redirects itself and expects apt to with this 103. Allowing apt
      # to handle the redirect makes it much harder to be certain we are validating the file we think we ought to be validating.
      # So we'll have to deal with the redirect ourselves.
      if($child_response_info->{Code} == 103) {
         @command_block = map { s/^URI:.*$/URI: $child_response_info->{'New-URI'}/r } @command_block;
         print $http_child_in @command_block;
      }
      elsif($child_response_info->{Code} < 201) {
         # an "in progress" notification; forward through
         print STDOUT inject_original_uri($original_uri_from_apt, @child_response_block);
      }
      else {
         # completion message; check if this is for InRelease and verify hash, or if for any other file just forward through.
         # for failure cases 'die' which stops apt in its tracks, otherwise it might ignore a failure if a local cached file is still available.
         if ($child_response_info->{Code} == 201 && $expected_hash) {
            if (validate_file($child_response_info->{Filename}, $expected_hash)) {
               print STDOUT inject_original_uri($original_uri_from_apt, @child_response_block);
            } else {
               die "\nHash mismatch for $child_response_info->{URI}\n";
            }
         }
         else {
            print STDOUT inject_original_uri($original_uri_from_apt, @child_response_block);
         }

         last;
      }
   }
}

close($http_child_in);
close($http_child_out);
waitpid($pid, 0);

sub read_block($fh) {
   my @lines;
   return @lines unless defined(my $first_line = <$fh>);
   push @lines, $first_line;
   while (my $line = <$fh>) {
      push @lines, $line;
      last if $line eq "\n";
   }
   return @lines;
}

sub parse_block(@block) {
   return {} unless @block;
   my $info = {};
   if ($block[0] =~ /^(\d+)\s+(.*)/) {
      $info->{Code} = $1;
      $info->{Description} = $2;
   }
   for my $line (@block[1 .. $#block]) {
      if ($line =~ /^([^:]+):\s*(.*)/) {
         $info->{$1} = $2;
      }
   }
   return $info;
}

#responses need to go back to apt with the URL it originally sent; since we're handling redirects internally need to touch this up
sub inject_original_uri($original_uri, @response_block) {
   return map { s/^URI:.*$/URI: $original_uri/r } @response_block;
}

sub validate_file($file_path, $expected_hash) {
   return 0 unless defined $file_path && -f $file_path;

   #can't use Digest::SHA as that package is not installed; call off to sha256sum instead
   my ($sha_out, $sha_in);
   my $sha_pid = open2($sha_out, $sha_in, 'sha256sum', $file_path);
   close $sha_in;

   my $output = <$sha_out>;
   close $sha_out;
   waitpid($sha_pid, 0);

   return 0 if $? != 0;  #command failed
   return 0 unless defined $output;

   chomp $output;
   my ($calculated_hash) = split /\s+/, $output;
   return 0 unless defined $calculated_hash;

   return $calculated_hash eq $expected_hash;
}
