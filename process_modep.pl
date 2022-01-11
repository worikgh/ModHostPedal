#!/usr/bin/perl -w
use strict;

# For temporary files to transfer commands to `control`
use File::Temp qw/tmpnam/;

my $VERBOSE = shift;
defined $VERBOSE or $VERBOSE  = 0;

## Where modep puts its pedal definitions
my $MODEP_PEDALS = "/var/modep/pedalboards";

## Where `control` is to be found
my $PATH_MI_ROOT = $ENV{PATH_MI_ROOT} or die "Define PATH_MI_ROOT";

## Reads the modep configuration files for pedals and sets them all up
## in mod-host.  For each pedal it writes a file in the current
## directory to implement the pedal by connecting jack IO

## Keyed by the name of a effect, values are the instance number 
my %effect_name_instance = ();

## Passed a prefix and a file name returns a array of mod-host command
## strings.  The returned strings have as instance numbers the effect
## name prefixed with the passed prefix (this allows the same effect
## to be used in different ways) 
sub process_file( $$ ) {
    my $prefix = shift or die;
    my $fn = shift or die;
    $VERBOSE and print STDERR "Process $prefix  $fn\n";
    -r $fn or die $!;
    open(my $fh, $fn) or die $!;
    my @lines = <$fh>;
    my @ret = ();
    
    ## Channels between effects and the outside 
    my %channels = ();
    my $channel = ''; # The last channel seen 

    my %effects = ();
    my $EFFECT = ""; # The last effect seen

    # The last port seen
    my $port = "";

    ## Keep track of sources and sinks to create Jack pipes
    my $source = undef;
    my $sink = undef;
    my @jack_pipes = ();

    ## Keep track of jack ports found so they can be distinguished
    ## from effects
    my %jack_ports = ();
    
    my $ln = 0;
    foreach my $line (@lines){
	$ln++;
	chomp $line;

	## Do not care about expanding prefixes
	$line =~ /^\s*\@prefix/ and next;

	## I am unsure what these are.
	if($line =~ /^<:[a-z]/ or
	   $line =~ /^<\S+\/:\S+>\s*$/ or
	    $line =~ /^\s+a\s+atom:.+/){
	    $EFFECT = undef;
	    $port = "";
	    next;
	}

	## These seem to be some sort of global effect
	$line =~ /^<:/ and next;
	
	## Root of a tree.  A connection
	if($line =~ /^_:(\S+)\s*$/){
	    ## Got the root of a tree
	    my $id = $1;
	    defined($channels{$id}) and die "$ln: '$line' Already a tree for $id ";
	    $channels{$id} = {};
	    $channel = $id;
	    next;
	}

	## Source for channel
	if($line =~ /^\s+ingen:tail\s+<(\S+)\/(\S+)>\s*[.;]\s*$/){
	    #     ingen:tail <_jcm800pre_/out> ;
	    my ($effect, $port_name) = ($1, $2);
	    $source = "$prefix:$effect/$port_name";
	    $jack_ports{$source} = 1;

	    if(defined($sink)){
		push(@jack_pipes, [$source, $sink]);
		$source = undef;
		$sink = undef;
	    }

	    defined($effects{$effect}) or $effects{$effect} = {
		ports => {
		}
	    };
	    defined($effects{$effect}->{ports}->{$port_name}) or
		$effects{$effect}->{ports}->{$port_name} = {
		    category => "Audio",
		    type => "Source"
	    };

	    ## What is this supposed to do?
	    # foreach my $pn (sort keys %{$effects{$effect}->{ports}}){
	    # 	if($effects{$effect}->{ports}->{$pn}->{type} eq "Sink"){
	    # 	    $effects{$effect}->{ports}->{$pn}->{other} = $effect;
	    # 	    $effects{$effect}->{ports}->{$effect}->{other} = $pn;
	    # 	}
	    # }
	    next;
	}

	## System source for channel
	if($line =~ /^\s+ingen:tail\s+<([^\/]+)>\s*[.;]\s*$/){
	    # ingen:tail <capture_1> ;
	    $source = $1;
	    $jack_ports{$source} = 1;
	    if(defined($sink)){
		push(@jack_pipes, [$source, $sink]);
		$source = undef;
		$sink = undef;
	    }
	    next;
	}

	## System sink for channel
	if($line =~ /^\s+ingen:head\s+<([^\/]+)>\s*[.;]\s*$/){
	    ## ingen:head <playback_1> .
	    $sink = $1; # playback_1
	    $jack_ports{$sink} = 1;
	    
	    if(defined($source)){
		push(@jack_pipes, [$source, $sink]);
		$source = undef;
		$sink = undef;
	    }
	    next;
	}
	
	## Sink for channel
	if($line =~ /^\s+ingen:head\s+<(\S+)\/(\S+)>\s*[.;]\s*$/){
	    my ($effect, $port_name) = ($1, $2);
	    $sink = "$prefix:$effect/$port_name";
	    $jack_ports{$sink} = 1;
	    if(defined($source)){
		push(@jack_pipes, [$source, $sink]);
		$source = undef;
		$sink = undef;
	    }
	    
	    defined($effects{$effect}) or $effects{$effect} = {
		ports => {
		}
	    };
	    defined($effects{$effect}->{ports}->{$port_name}) or

		$effects{$effect}->{ports}->{$port_name} = {
		    category => "Audio",
		    type => "Sink"
	    };

	    # foreach my $pn (sort keys %{$effects{$effect}->{ports}}){
	    # 	if($effects{$effect}->{ports}->{$pn}->{type} eq "Source"){
	    # 	    $effects{$effect}->{ports}->{$pn}->{other} = $effect;
	    # 	    $effects{$effect}->{ports}->{$effect}->{other} = $pn;
	    # 	}
	    # }
	    next;
	}
	
	# The start of a effect
	if($line =~ /^<([^\/\s]+)>\s*$/ and !defined($jack_ports{$1})){
	    # An effect

	    ## Any effects that are used will be in here.  If not here ignore it
	    defined($effects{$1}) or next;

	    $EFFECT = $1;
	    $port = undef;
	    next;
	}

	if(defined($EFFECT)){
	    if($line =~ /^\s+ingen:enabled ([truefals]{4,5})\s*;\s*$/){
		$effects{$EFFECT}->{enabled} = $1;
		next;
	    }

	    ## URL for effect
	    if($line =~ /^\s+lv2:prototype\s+<(\S+)>\s*;\s*$/){
		$effects{$EFFECT}->{URL} = $1;
		next;
	    }
	}

	## A effect/port section: <CABINET/CLevel> or <Degrade/left_in>
	if($line =~ /^<(\S+)\/(\S+)>\s*$/){
	    ## Could be a audio port or could be a control port
	    if(defined($effects{$1})){
		## Only do this for effects that are connected
		$EFFECT =  $1; # CABINET or Degrade
		$port = $2; # CLevel or left_in
	    }
	    next;
	}

	## Detect if port a control or audio port
	if($EFFECT && $port &&
	   ($line =~ /^\s+a\s+lv2:(Audio)Port\s*,\s*$/ or
	   $line =~ /^\s+a\s+lv2:(Control)Port\s*,\s*$/)){
	    $effects{$EFFECT}->{ports}->{$port}->{type} = $1;
	    next;
	}

	# Input or output?
	if($EFFECT && $port && (
	       $line =~ /^\s+lv2:(Input)Port\s*\.\s*$/ or
	       $line =~ /^\s+lv2:(Output)Port\s*\.\s*$/)
	   ){
	    $effects{$EFFECT}->{ports}->{$port}->{direction} = $1;
	    next;
	}

	# Value for a control port
	if($EFFECT && $port && $line =~ /^\s+ingen:value\s+(\S+)\s*;\s*$/){
	    $effects{$EFFECT}->{ports}->{$port}->{value} = $1;
	    next;
	}
    }

    ## Make lines to add effects
    foreach my $e (sort grep {/\S/} keys %effects){
	# "$prefix:Effect" name stands in for instance number
	my $instance_number = "$prefix:$e";

	# Later these will be used to assign integers > 0 
	$effect_name_instance{$instance_number} = -1;

	my $_e1 = $effects{$e};
	my $_url = $_e1->{URL};
	my $cmd = "add $_url $instance_number";
	# print STDERR "\$cmd $cmd\n";
	my $line = "$cmd";
	push(@ret, $line);

	    ## Make param set lines

	my @ports = sort keys %{$effects{$e}->{ports}};
	foreach my $p (@ports){
	    if($effects{$e}->{ports}->{$p}->{type} eq 'Control'){
		if($effects{$e}->{ports}->{$p}->{direction} eq "Output"){
		    ## No value for a output port.  Not used here
		    next;
		}
		my $value = $effects{$e}->{ports}->{$p}->{value};
		$line = "param_set $instance_number $p $value";
		push(@ret, $line);
	    }
	}
    }

    ## Add jack connections
    foreach my $ss (@jack_pipes){
	my $line = "jack_connect $ss->[0] $ss->[1]";
	push(@ret, $line);
    }
    return @ret;
}

## Get the ttl file that holds all the pedal board definitions
sub get_board_ttl( $$$ ){
   my ($root, $name, $board) = @_;
    my $fn = "$root/$name/$board.ttl";
    if(! -r $fn){
	$board = ucfirst($board);
	$fn = "$root/$name/$board.ttl";
    }
    return $fn;
}

## This prefix is used so the same LV2 units can be used more than
## once for different pedal boards
my $prefix = 'a';

## All directories with a ".pedalboard" suffix have a pedal board
## definition
opendir(my $dir, $MODEP_PEDALS) or die $!;
my @names = grep { /\.pedalboard$/ } readdir($dir);

## Put commands to pass to `control` here
my @commands = ();

foreach my $name (sort @names){
    $name =~ /^(\S+)\.pedalboard$/ or die $name;
    my $board = $1;
    $VERBOSE  and print STDERR "board: $board\n";
    my $fn = &get_board_ttl($MODEP_PEDALS, $name, $board);
    
    # print STDERR "Process: $fn\n";
    my @foo = &process_file($prefix, $fn);
    push(@commands, [$board, \@foo]);
    $prefix++;
}

## Replace instance numbers with integers
my $inst = 1; ## Initial number
foreach my $key (sort keys %effect_name_instance){
    $VERBOSE  and print STDERR $effect_name_instance{$key}." -> $inst\n";
    $effect_name_instance{$key} = $inst++;
}

## Now have propper instance numbers.  Apply them

my %pedal_settings = ();
foreach my $cc (@commands){
    my $name = $cc->[0];
    my @cmds = @{$cc->[1]};
    defined($pedal_settings{$name}) and die "$name";
    $pedal_settings{$name} = [];
    foreach my $c (@cmds){
	if($c =~ /^param_set/ ||
	   $c =~ /^add /) {

	    foreach my $in(sort keys %effect_name_instance){
		$c =~ s/$in/$effect_name_instance{$in}/g;
	    }
	}elsif($c =~ /jack_con/){
	    foreach my $in(sort keys %effect_name_instance){
		$c =~ s/$in\//effect_$effect_name_instance{$in}:/g;
	    }
	}	
	push(@{$pedal_settings{$name}}, $c);
    }
}

## Set up each possible pedal

## Commeands to run now to set up the pedal
my %control_commands = ();

## Commands to run when the pedal is used
my %pedal_commands = ();

foreach my $name ( sort keys %pedal_settings){

    $VERBOSE  and print STDERR "Set up $name\n";
    $pedal_commands{$name} = [];
    $control_commands{$name} = [];
    
    my @commands = @{$pedal_settings{$name}};

    foreach my $cmd (@commands){
	$VERBOSE  and print STDERR "cmd: $cmd\n";
	if($cmd =~ /^jack_connect\s+(.+)\s*$/){
	    ## If this command involves system:capture or system:playback
	    ## then it is to be run at pedal use time.  Else run it now
	    my $jack_cmd = $1;

	    my $flag = 0;
	    $jack_cmd =~ s/playback/system:playback/ and $flag = 1;
	    $jack_cmd =~ s/capture/system:capture/ and $flag = 1;
	    if($flag == 1){
		## To be run at pedal use time.
		push(@{$pedal_commands{$name}}, $jack_cmd);
	    }else{
		push(@{$control_commands{$name}}, "jack $1");
	    }
	}else{
	    ## All mod-host  commands run now
	    push(@{$control_commands{$name}}, "mh $cmd");
	}
    }
}
# print "Run now: ".join ("\n",
# 			map {"$_\n\t" . join("\n\t\t",
# 					     @{$control_commands{$_}})."\n"}
#     sort keys %control_commands) . "\n";
# print "Pedals:\n\t";
# foreach my $name (sort keys %pedal_commands){
#     print "$name\n\t\t";
#     foreach my $cmd (@{$pedal_commands{$name}}){
# 	print "$cmd\n\t\t";
#     }
#     print "\n";
# }

my $pedal_dir = "$PATH_MI_ROOT/PEDALS";

-d $pedal_dir or mkdir $pedal_dir or die "$!: Cannot mkdir $pedal_dir";

## Delete old pedals
opendir(my $pedals, $pedal_dir) or die "$!: $pedal_dir ";
my @files =
    grep{/^[a-zA-Z0-9_]\S/} ## Filter out the pedal links which are single character
readdir($pedals);
foreach my $file (@files){
    
    my $df = "$pedal_dir/$file"; 
    unlink $df or die "$!: $file";
}

foreach my $name (sort keys %control_commands){
    
    ## Loop over each effect and set it up in `mod-host` using `control`
    
    # print STDERR  "Setting up $name pedal\n";
    my $cmds = join("\n", @{$control_commands{$name}})."\n";
    my ($now, $tmpFn) = tmpnam() or die $!;
    print $now $cmds;
    # print STDERR  $cmds;
    seek($now, 0, 0);

    my @res = `$PATH_MI_ROOT/control $tmpFn`;
    # print STDERR  "Finished with control \$? $?\n";
    my $_name = "$pedal_dir/$name";
    if(! grep {/FAIL/} @res ){
	# print STDERR "\$_name: $_name\n";
	open(my $pedal, ">$_name") or die "$!: $_name";
	print $pedal join("\n", @{$pedal_commands{$name}})."\n";
    }else{
	print STDERR join("\n", @res)."\n";
	unlink $_name;
    }
    # print STDERR "Name done: $name\n";
}
