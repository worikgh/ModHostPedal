/* https://www.linuxjournal.com/article/6429?page=0,1 */
/*
 Userspace driver for a simple three key USB keyboard.  Hard coded to
 three keys 'A', 'B', 'C'.  When a key is pressed an LV2 effect chain
 is enabled, and an old one disabled
 */
#include <linux/limits.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <jack/jack.h>
#include <linux/input.h>
#include <linux/limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
jack_client_t *CLIENT;

// Reset this to exit main loop
int RUNNING = 1;

struct jack_connection;
struct pedal_config;

void Log(char * sp, ...);
int load_pedal(char);
void print_pedal(char pedal);
void clear_jack();
void initialise_pedals();
void destroy_pedals();
int connected(const char * port_a, const char * port_b);
void free_connection(struct jack_connection  * jc);
void print_connections();
void clean_cfg(const struct pedal_config * pc_in, struct pedal_config * pc_ret);

struct jack_connection {
  char * ports[2];
};
struct pedal_config {
  struct jack_connection * connections;
  unsigned n_connections;
};

struct Pedals {
  struct pedal_config pedal_configA;
  struct pedal_config pedal_configB;
  struct pedal_config pedal_configC;
};
struct Pedals pedals;




/*
   Make the jack connections (from the system input to effect, from
   effect to system output) that enables an effect. This is done
   before the old connections for the pedal being replaced is
   disconnected/deimplemented so ensure that any connections shared
   between them are removed from the old configuration.

   If the first pedal is being implemented pass NULL for old_pedal
*/
void implement_pedal(char * pedal, char * old_pedal){
#ifdef VERBOSE
  Log( "%s:%d implement_pedal %c -> %c\n",
	  __FILE__, __LINE__, old_pedal ? *old_pedal : '_', *pedal);
#endif
  if ( pedal == NULL ) {
    // TODO Is this possible? Should this be a crash?
    return;
  }
  
  // Get the configuration data for the old and new pedal
  struct pedal_config * pc;
  struct pedal_config * opc = NULL;
  switch (*pedal) {
  case 'A':
    pc = &pedals.pedal_configA;
    break;
  case 'B':
    pc = &pedals.pedal_configB;
    break;
  case 'C':
    pc = &pedals.pedal_configC;
    break;
  default:
    Log( "%s:%d implement_pedal Unknown: %c\n",
	    __FILE__, __LINE__, pedal ? *pedal : '!');
    assert(0);
  }

  if (old_pedal != NULL){
    switch (*old_pedal) {
    case 'A':
      opc = &pedals.pedal_configA;
      break;
    case 'B':
      opc = &pedals.pedal_configB;
      break;
    case 'C':
      opc = &pedals.pedal_configC;
      break;
    default:
      Log( "%s:%d implement_pedal Unknown: %c\n",
	      __FILE__, __LINE__, old_pedal ? *old_pedal : '!');
      assert(0);
    }
    clean_cfg(pc, opc);
  }

  // Connect the new pedal
  for (unsigned i = 0; i < pc->n_connections; i++){
    char * src_port = pc->connections[i].ports[0];
    char * dst_port = pc->connections[i].ports[1];
    int r = 0;
    if(!connected(src_port, dst_port)){
      r = jack_connect(CLIENT, src_port, dst_port);
    }else{
#ifdef VERBOSE
      Log( "%s:%d src_port: %s dst_port %s already connected\n",
	      __FILE__, __LINE__, src_port, dst_port);
#endif
    }

    if(r != 0 && r != EEXIST){
      if(!connected(src_port, dst_port)){
	Log( "%s:%d FAILURE %c %s => %s  jack_connect: %d\n",
		__FILE__, __LINE__, *pedal, src_port, dst_port, r);
#ifdef VERBOSE
	 print_connections();
#endif
	// Bail out after an error
	exit(-1);
      }
    }
  }
#ifdef VERBOSE
  Log( "%s:%d END implement_pedal %c -> %c\n",
	  __FILE__, __LINE__, old_pedal ? *old_pedal : '_', *pedal);
#endif
}

void deimplement_pedal(char * pedal){
#ifdef VERBOSE
  Log( "%s:%d\n", __FILE__, __LINE__);
#endif
  if ( pedal == NULL ) {
    return;
  }
  
  struct pedal_config * pc;
  switch (*pedal) {
  case 'A':
    pc = &pedals.pedal_configA;
    break;
  case 'B':
    pc = &pedals.pedal_configB;
    break;
  case 'C':
    pc = &pedals.pedal_configC;
    break;
  default:
    assert(0);
  }
  for (unsigned i = 0; i < pc->n_connections; i++){

    // The naems of the jack ports to disconnet
    if(pc->connections[i].ports[0]){
      // But only if they are existing.  They might have been deleted
      // if they are in the new configuration
      char * src_port = pc->connections[i].ports[0];
      char * dst_port = pc->connections[i].ports[1];

      // Check that the connection exists before disconnecting it.
    
      /* jack_port_t * s = jack_port_by_name(CLIENT, src_port); */
      /* jack_port_t * d = jack_port_by_name(CLIENT, dst_port); */
      /* if(!jack_port_connected_to(s, dst_port)){ */
      /*   Log( "WON'T disconnect %s -> %s\n", src_port, dst_port); */
      /* }else if(!jack_port_connected_to(d, src_port)){ */
      /*   Log( "WON'T disconnect %s -> %s\n", dst_port, src_port); */
      /* }else{ */

      if(connected(src_port, dst_port)){
	int r = jack_disconnect(CLIENT, src_port, dst_port);  
	if(r != 0 && r != EEXIST){

	  /*
	    jack is returning -1 even though it has disconected the
	    connection.  So double check.  If the ports are still
	    connected then exit
	   */
	  int bail_out = connected(src_port, dst_port);
	  
 	  Log("%s:%d: FAILURE  Pedal: %c %s -> %s  "
	      "jack_disconnect returned %d %s\n",
	      __FILE__, __LINE__, *pedal, src_port, dst_port, r,
	      bail_out ? " Bailing out" : " Every thing is OK");
	  if(bail_out) {
	    exit(-1);
	  }
	}
      }
    }
  }
#ifdef VERBOSE
  Log( "%s:%d\n", __FILE__, __LINE__);
#endif
}

// Test if these two ports are connected
int connected(const char * port_a, const char * port_b) {
  jack_port_t * jpt_a = jack_port_by_name(CLIENT, port_a);
#ifdef VERBOSE
  jack_port_t * jpt_b  = jack_port_by_name(CLIENT, port_b);
  int res1 = jack_port_connected_to(jpt_a, port_b);
  int res2 = jack_port_connected_to(jpt_b, port_a);
  Log( "%s:%d port_a: %s port_b: %s res1: %d res2: %d\n",
	  __FILE__, __LINE__, port_a, port_b, res1, res2);
#endif
  return jack_port_connected_to(jpt_a, port_b);
}

/* Tests if the `bit`th bit is set in `array.  Used to detect pedaal
   depressions` */
int test_bit(unsigned bit, uint8_t *array)
{
  return (array[bit / 8] & (1 << (bit % 8))) != 0;
}


void print_ports(){
  const char ** inp = jack_get_ports(CLIENT, "system", "audio", 0);
  for(unsigned i = 0; inp[i]; i++){
    printf("Port %d: %s\n", i, inp[i]);    
    jack_port_t * port = jack_port_by_name(CLIENT, inp[i]);
    const char ** connections = jack_port_get_all_connections(CLIENT, port);
    if(connections){
      for(int j = 0; connections[j]; j++){
	printf("\t-> %s\n", connections[i]);
      }
    }
  }
}

int signaled = 0;
static void signal_handler(int sig)
{
  signaled = 1;
}

void jack_shutdown (void *arg)
{
  exit (1);
}

// Add a jack connection between `jc1` and `jc2` for a pedal defined
// in `pedal` into the configuration structure
void add_pedal_effect(char pedal, const char * jc1, const char* jc2){

#ifdef VERBOSE
  Log("add_pedal_effect(%c, %s, %s)\n", pedal, jc1, jc2);
#endif
  struct pedal_config * pc = NULL;
  switch (pedal) {
  case 'A': 
    pc = & pedals.pedal_configA;
    break;
  case 'B':
    pc = & pedals.pedal_configB;
    break;
  case 'C':
    pc = & pedals.pedal_configC;
    break;
  }

  assert(pc != NULL);

  pc->n_connections++;
  pc->connections =
    realloc(pc->connections,
	    pc->n_connections * (sizeof (struct jack_connection)));
  assert(pc->connections);
  unsigned jc1_len = strlen(jc1)+1;
  unsigned jc2_len = strlen(jc2)+1;

  // Sanity check
  unsigned MAX_COMMAND = 1024;
  assert(jc1_len < MAX_COMMAND);
  assert(jc2_len < MAX_COMMAND);

  pc->connections[pc->n_connections - 1].ports[0] =
    malloc(sizeof (char)  * jc1_len);

  pc->connections[pc->n_connections - 1].ports[1] =
    malloc(sizeof (char)  * jc2_len);
  
  strncpy(pc->connections[pc->n_connections - 1].ports[0], jc1, jc1_len);
  strncpy(pc->connections[pc->n_connections - 1].ports[1], jc2, jc2_len);

}

/* Called on setup and when signaled to set up pedal effects, this is
   one line in the configuration file. Pedals are defined by jack
   plumbing.  Each line in a pedal file is the destination (src/sink)
   of a jack pipe.  This sets up those pipes 
*/
void process_line(char pedal, char * line){
  const char * src_port, * dst_port;
  char * tok = strtok(line, " ");
  src_port = tok;
  assert(src_port);
  dst_port = strtok(NULL, " ");
  assert(dst_port);
  add_pedal_effect(pedal, src_port, dst_port);
}

/* Disconnect jack pipes to stdin and stdout so the pedal can replace
   them .  Do it after the new peda has been connected

   So when new pedal selected grab the system jack connections and put
   them here to discomment after new pedal established

*/
const char ** ports_to_disconnect;
void clear_jack(){
  ports_to_disconnect =  jack_get_ports(CLIENT, "system",
					"32 bit float mono audio", 0 );
  int i;
  for(i = 0; ports_to_disconnect[i]; i++){
    /* Log( "ports_to_disconnect[%d]: %s\n", i, ports_to_disconnect[i]); */
    jack_port_disconnect(CLIENT, jack_port_by_name(CLIENT,
						   ports_to_disconnect[i]));
  }
  if(ports_to_disconnect) {
    jack_free(ports_to_disconnect);
  }
}

/* Called on setup and when signaled to set up pedal effects.  Reads
 * the description of what this pedal (passed in `p`) does from
 * PEDALS/${p}
 */
int load_pedal(char p){
  int i;
  FILE * fd;
  char  scriptname[PATH_MAX];

  const char * path_mi_root;
  int ch;

  /* We do not want buffer overruns... */
  const uint LINE_MAX = 1024;
  char line[LINE_MAX];
  char pedal;
  path_mi_root = getenv("PATH_MI_ROOT");
  
  pedal = p;
  switch (p){
  case 'A':
    break;
  case 'B':
    break;
  case 'C':
    break;
  default:
    assert(0);
    break;
  }
  sprintf(scriptname, "%s/PEDALS/%c", path_mi_root, pedal);

  Log( "Opening script: %s\n", scriptname);
  fd = fopen(scriptname, "r");
  assert(fd);
  i = 0;
  while((ch = fgetc(fd)) != EOF && i < LINE_MAX){  /* while(!feof(fd)){ */
    
    line[i] = ch;
    if((ch >= 'a'  && ch <= 'z') ||
       (ch >= 'A'  && ch <= 'Z') ||
       (ch >= '0' && ch <= '9') ||
       ch == '_' || ch == ' ' || ch == ':' || ch == '\n'){

      if(line[i] == '\n'){
	line[i] = '\0';
	process_line(pedal, line);
	i = 0;

      }else{
	i++;
      }	
    }else{
      // FIXME  I do not think this is ever called
      Log("Bad character: %c Position: %d\n", ch, i);
    }
  }

  // Ensure that no line overflowed the buffer
  if(i >= LINE_MAX){
    Log("i: %d script: %s\n", i, scriptname);
    assert(i < LINE_MAX);
  }
  return 0;
}

void jack_error_cb(const char * msg){
  Log( "JACK ERROR: %s\n", msg);
}

int main(int argc, char * argv[]) {

  // Defined in jack.h(?)
  jack_status_t status;

  unsigned buff_size = 1024;
  char buf[buff_size];

  fd_set rfds;
  struct timeval tv;

  int retval, res;
  unsigned yalv;//, last_yalv;
  uint8_t key_b[KEY_MAX/8 + 1];
  char * mi_root;
  char home_dir[PATH_MAX + 1];

  struct sigaction act;
  memset (&act, 0, sizeof (act));
  act.sa_handler = signal_handler;
  act.sa_flags = SA_RESTART | SA_NODEFER;
  if (sigaction (SIGHUP, &act, NULL) < 0) {
    perror ("sigaction");
    exit (-1);
  }


  // Initialise the definitions of pedals
  // Signal with HUP to change
  initialise_pedals();
  
  mi_root = getenv("PATH_MI_ROOT");
  assert(snprintf(home_dir, PATH_MAX, "%s", mi_root) <= PATH_MAX);
  chdir(home_dir);

  pid_t pid = getpid();
  int fd_pid = open(".driver.pid", O_WRONLY);
  assert(fd_pid);
  
  // Lock the file because the front end used this to communicate with
  // this programme.  
  if(!fcntl(fd_pid, F_SETLK, F_WRLCK)){
    Log("%s:%d: Error %s\n", __FILE__, __LINE__, strerror(errno));
    exit(-1);
  }

  int pid_res = dprintf(fd_pid, "%d", pid);

  if(close(fd_pid) < 0){
    Log("%s:%d: Error %s\n", __FILE__, __LINE__, strerror(errno));
    return -1;
  }	

  Log( "Wrote pid: %d. Error: %s\n", pid, strerror(errno));
  assert(pid_res > 0);

  
  /* printf("Driver getting started in %s\n", home_dir); */

  /* Set up the client for jack */
  CLIENT = jack_client_open ("client_name", JackNullOption, &status);
  if (CLIENT == NULL) {
    fprintf (stderr, "jack_client_open() failed, "
	     "status = 0x%2.0x\n", status);
    if (status & JackServerFailed) {
      fprintf (stderr, "Unable to connect to JACK server\n");
    }
    exit (1);
  }

  // The keyboard/pedal
  int fd;
  fd = open("/dev/input/event0", O_RDONLY);
  if(fd < 0) {
    printf("Error %s\n", strerror(errno));
    return fd;
  }

  unsigned last_yalv = 0;

  char * current_pedal = NULL;
  char A = 'A', B = 'B', C = 'C';

#ifdef PROFILE
  int loop_limit = 0;
#endif
  Log("Starting main loop\n");
  while(RUNNING == 1){
#ifdef PROFILE
     if(loop_limit++ > 12){
      RUNNING = 0;
    }
#endif
    tv.tv_sec = 200;
    tv.tv_usec = 0;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    retval = select(fd+1, &rfds, NULL, NULL, &tv);

    if(retval < 0){
      Log("select Error %s\n", strerror(errno));

      // TODO: What is this constant: 4?
      if(errno == 4){
	
	// Interupted by a signal
	Log( "%s:%d: signaled: %d\n",
		__FILE__, __LINE__, signaled);
	if(signaled){
	  fprintf(stderr, "signaled\n");
	  destroy_pedals();
	  initialise_pedals();
	}
	signaled = 0;
	continue;
      }
      return -1;
    }else if(retval == 0){
#ifdef VERBOSE
      Log("Heartbeat...");
#endif
      continue;
    }

    // Read the keyboard
    memset(key_b, 0, sizeof(key_b));
    if(ioctl(fd, EVIOCGKEY(sizeof(key_b)), key_b) == -1){
      printf("IOCTL Error %s\n", strerror(errno));
      return -1;
    }
    /* Log( "IOCTL returnes\n"); */
    
    for (yalv = 0; yalv < KEY_MAX; yalv++) {
      if (test_bit(yalv, key_b)) {
	/* the bit is set in the key state */
	if(last_yalv != yalv){
	  /* Only when it changes */
	  char * old_pedal = current_pedal;

	  if(yalv == 0x1e){
	    current_pedal = &A;
	  }else if(yalv == 0x30){
	    current_pedal = &B;
	  }else if(yalv == 0x2e){
	    current_pedal = &C;
	  }	    
	  last_yalv = yalv;
	  struct timeval a, b, c;

	  gettimeofday(&a, NULL);

	  implement_pedal(current_pedal, old_pedal);

	  gettimeofday(&b, NULL);

	  deimplement_pedal(old_pedal);

	  gettimeofday(&c, NULL);

	  Log("Implement %c: %ld\n", *current_pedal,
	      ((b.tv_sec - a.tv_sec) * 1000000) +
	      (b.tv_usec - a.tv_usec));

	  
	  Log( "Deimplement %c: %ld\n", old_pedal?*old_pedal:'-',
		  ((c.tv_sec - b.tv_sec) * 1000000) +
		  (c.tv_usec - b.tv_usec));
	  Log("Total: %ld\n", ((c.tv_sec - a.tv_sec) * 1000000) +
	  	 (c.tv_usec - a.tv_usec));
	}

	// Write a record of the pedal for the front end to read.  If
	// there is one.
	int fd_pedal;
	char file_name[PATH_MAX];
	const char * path_mi_root = getenv("PATH_MI_ROOT");
	snprintf(file_name, PATH_MAX, "%s/PEDALS/.PEDAL", path_mi_root);
	fd_pedal = open(file_name, O_WRONLY); // File must exist
	if(fd < 0) {
	  Log("%s:%d: Error %s\n", __FILE__, __LINE__, strerror(errno));
	  return fd;
	}

	// Lock the file so the front end does not get confused. It
	// will lock it too
	if(!fcntl(fd_pedal, F_SETLK, F_WRLCK)){
	  Log("%s:%d: Error %s\n", __FILE__, __LINE__, strerror(errno));
	  return -1;
	}

	dprintf(fd_pedal, "%c", current_pedal ? *current_pedal : ' ');

	if(close(fd_pedal) < 0){
	  Log("%s:%d: Error %s\n", __FILE__, __LINE__, strerror(errno));
	  return -1;
	}	
      }
    }

    /* Consume what can be read from fd */
    res = read(fd, &buf, buff_size);
    if(res < 0){
      printf("Read error: %s\n", strerror(errno));
      return res;
    }else if(res == 0){
      printf("Nothing to read\n");
    }      
    /* printf("That was a pedal\n"); */
  }
  Log( "After main loop.  RUNNING: %d\n", RUNNING);
  return 0;
}


//+===========+++++========++++++=================
// useful but borring functions
//

void Log(char * sp, ...){

  const char * log_fn = "/tmp/driver.log";
  const unsigned MAX_LOG = 2048;
  char LOGBUFFER[MAX_LOG];

  va_list argptr;
  va_start(argptr, sp);
  vsnprintf(LOGBUFFER, MAX_LOG, sp, argptr);
     //int fd = open(log_fn, O_APPEND|O_CREAT, 0644); //S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH );
     int fd = open(log_fn,  O_WRONLY | O_CREAT | O_APPEND , 0644);
  if(fd < 0){
    fprintf(stderr, "%s:%d: Failed to open %s.  Error: %s\n",
	    __FILE__, __LINE__, log_fn, strerror(errno));
    exit(fd);
  }


  int res = dprintf(fd, LOGBUFFER);
  if(res != strlen(LOGBUFFER)){
    fprintf(stderr, "%s:%d: Failed to write log %s d: %d fd: %d Message: %s\n",
	    __FILE__, __LINE__, strerror(errno), res, fd, LOGBUFFER);
    exit(-1);
  }

  if(close(fd)){
    fprintf(stderr, "%s:%d: Failed to close log  %s\n",
	    __FILE__, __LINE__, strerror(errno));
    exit(-1);
  }
}

void print_connections() {
  const char **ports, **connections;
  ports = jack_get_ports (CLIENT, NULL, NULL, 0);
  for (int i = 0; ports && ports[i]; ++i) {
    /* jack_port_t *port = jack_port_by_name (CLIENT, ports[i]); */
    if ((connections = jack_port_get_all_connections
	 (CLIENT, jack_port_by_name(CLIENT, ports[i]))) != 0) {
      for (int j = 0; connections[j]; j++) {
	Log( "CONNECTION\t%s => %s\n", ports[i], connections[j]);
      }
      jack_free (connections);
    }
  }
  if(ports){
    jack_free(ports);
  }
}  

void print_pedal(char pedal){
  struct pedal_config * pc;
  switch (pedal) {
  case 'A':
    pc = &pedals.pedal_configA;
    break;
  case 'B':
    pc = &pedals.pedal_configB;
    break;
  case 'C':
    pc = &pedals.pedal_configC;
    break;
  default:
    assert(0);
  }
  Log( "Pedal %c:\n\t", pedal);
  for(unsigned i = 0; i < pc->n_connections; i++){
    struct jack_connection * jcp = &pc->connections[i];
    Log( ">A> %s -> %s\n\t", jcp->ports[0], jcp->ports[1]);
  }
  Log( "\n");
}


// Called on set up and when signaled to set up the pedals.  Define
// what they do
void initialise_pedals(){
  pedals.pedal_configA.n_connections = 0;
  pedals.pedal_configA.connections = NULL;
  pedals.pedal_configB.n_connections = 0;
  pedals.pedal_configB.connections = NULL;
  pedals.pedal_configC.n_connections = 0;
  pedals.pedal_configC.connections = NULL;
  load_pedal('A');
  load_pedal('B');
  load_pedal('C');
}


void _destroy_pedal(struct pedal_config * pc){
  clear_jack();
  if(  pc->n_connections > 0) {
    for(unsigned i = 0; i < pc->n_connections; i++){
      free_connection(&pc->connections[i]);
      /* if(pc->connections[i].ports[0]){ */
      /* 	free(pc->connections[i].ports[0]); */
      /* } */
      /* if(pc->connections[i].ports[1]){ */
      /* 	free(pc->connections[i].ports[1]); */
      /* } */
    }
    free(pc->connections);
    pc->connections= NULL;
    pc->n_connections = 0;
  }
}

void destroy_pedals() {
  _destroy_pedal(&pedals.pedal_configA);
  _destroy_pedal(&pedals.pedal_configB);
  _destroy_pedal(&pedals.pedal_configC);  
}

void free_connection(struct jack_connection  * jc){
  if(jc->ports[0]){
    free(jc->ports[0]);
    jc->ports[0] = NULL;
  }
  if(jc->ports[1]){
    free(jc->ports[1]);
    jc->ports[1] = NULL;
  }
}

void clean_cfg(const struct pedal_config * pc_in, struct pedal_config * pc_ret){
  for (unsigned i = 0; i < pc_in->n_connections; i++){
    char * src_port = pc_in->connections[i].ports[0];
    char * dst_port = pc_in->connections[i].ports[1];
    for (unsigned j = 0; j < pc_ret->n_connections; j++){
      char * s = pc_ret->connections[j].ports[0];
      char * d = pc_ret->connections[j].ports[1];

      if(s && d){
	if((!strcmp(s, src_port) && !strcmp(d, dst_port)) ||
	   (!strcmp(d, src_port) && !strcmp(s, dst_port))){
	  // This connection is in pc_in and pc_ret so remove from
	  // pc_ret
	  free_connection(&pc_ret->connections[i]);
	}
      }else{
	Log( "%s:%d ports s: %s d: %s %s/%s\n",
		__FILE__, __LINE__,
		s?" OK ":" NULL ",
		d?" OK ":" NULL ",
		src_port, dst_port);
      }
    }
  }
}
