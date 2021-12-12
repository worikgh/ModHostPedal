/* https://www.linuxjournal.com/article/6429?page=0,1 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <jack/jack.h>
#include <linux/input.h>
#include <linux/limits.h>
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

int load_pedal(char);

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
  fprintf(stderr, "Pedal %c:\n\t", pedal);
  for(unsigned i = 0; i < pc->n_connections; i++){
    struct jack_connection * jcp = &pc->connections[i];
    fprintf(stderr, ">A> %s -> %s\n\t", jcp->ports[0], jcp->ports[1]);
  }
  fprintf(stderr, "\n");
}
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

void clear_jack_1();
void _destroy_pedal(struct pedal_config * pc){
  clear_jack_1();
  if(  pc->n_connections > 0) {
    for(unsigned i = 0; i < pc->n_connections; i++){
      free(pc->connections[i].ports[0]);
      free(pc->connections[i].ports[1]);
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

void add_pedal_effect(char pedal, const char * jc1, const char* jc2){
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

void print_connections() {
  const char **ports, **connections;
  ports = jack_get_ports (CLIENT, NULL, NULL, 0);
  for (int i = 0; ports && ports[i]; ++i) {
    /* jack_port_t *port = jack_port_by_name (CLIENT, ports[i]); */
    if ((connections = jack_port_get_all_connections
	 (CLIENT, jack_port_by_name(CLIENT, ports[i]))) != 0) {
      for (int j = 0; connections[j]; j++) {
	printf("CONNECTION\t%s => %s\n", ports[i], connections[j]);
      }
      jack_free (connections);
    }
  }
  if(ports){
    jack_free(ports);
  }
}  

void implement_pedal(char * pedal){
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
    char * src_port = pc->connections[i].ports[0];
    char * dst_port = pc->connections[i].ports[1];

    // Encure that neither src_port or dst_port involved in any
    // connections
    
    int r = jack_connect(CLIENT, src_port, dst_port);
    if(r != 0 && r != EEXIST){
      fprintf(stderr, "FAILURE %d %c %s %s  jack_connect returned %d\n",
	      __LINE__, *pedal, src_port, dst_port, r);
      print_connections();

      // Bail out after an error
      RUNNING = 0;
    }
  }
}

// Test if these two ports are connected
int connected(const char * port_a, const char * port_b) {
  jack_port_t * jpt_a = jack_port_by_name(CLIENT, port_a);
  return jack_port_connected_to(jpt_a, port_b);
}

void deimplement_pedal(char * pedal){
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
    char * src_port = pc->connections[i].ports[0];
    char * dst_port = pc->connections[i].ports[1];

    // Check that the connection exists before disconnecting it.
    
    /* jack_port_t * s = jack_port_by_name(CLIENT, src_port); */
    /* jack_port_t * d = jack_port_by_name(CLIENT, dst_port); */
    /* if(!jack_port_connected_to(s, dst_port)){ */
    /*   fprintf(stderr, "WON'T disconnect %s -> %s\n", src_port, dst_port); */
    /* }else if(!jack_port_connected_to(d, src_port)){ */
    /*   fprintf(stderr, "WON'T disconnect %s -> %s\n", dst_port, src_port); */
    /* }else{ */
    if(connected(src_port, dst_port)){
      int r = jack_disconnect(CLIENT, src_port, dst_port);  
      if(r != 0 && r != EEXIST){
	fprintf(stderr, "FAILURE %d %c %s %s  jack_disconnect returned %d\n",
		__LINE__, *pedal, src_port, dst_port, r);
	print_connections();
	RUNNING = 0;
      }
    }
  }
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

/* Pedals are defined by jack plumbing.  Each line in a pedal file is
   the destination (src/sink) of a jack pipe.  This sets up those
   pipes */
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
void clear_jack_1(){
  ports_to_disconnect =  jack_get_ports(CLIENT, "system",
					"32 bit float mono audio", 0 );
  int i;
  for(i = 0; ports_to_disconnect[i]; i++){
    /* fprintf(stderr, "ports_to_disconnect[%d]: %s\n", i, ports_to_disconnect[i]); */
    jack_port_disconnect(CLIENT, jack_port_by_name(CLIENT,
						   ports_to_disconnect[i]));
  }
  if(ports_to_disconnect) {
    jack_free(ports_to_disconnect);
  }
}

void clear_jack_2() {
}

/* Called when a pedal is depressed. The argument defines the pedal */
int load_pedal(char p){
  int i;
  FILE * fd;
  char  scriptname[PATH_MAX];

  const char * path_mi_root;
  char ch;

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

  fprintf(stderr, "Opening script: %s\n", scriptname);
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
      break;
    }
  }

  // Ensure that no line overflowed the buffer
  assert(i < LINE_MAX);
  
  return 0;
}

void jack_error_cb(const char * msg){
  fprintf(stderr, "JACK ERROR: %s\n", msg);
}

int main(int argc, char * argv[]) {
  jack_status_t status;
  fd_set rfds;
  struct timeval tv;
  int retval, res;
  unsigned buff_size = 1024;
  char buf[buff_size];
  unsigned yalv, last_yalv;
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
  FILE * fd_pid = fopen(".driver.pid", "w");
  assert(fd_pid);
  
  int pid_res = fprintf(fd_pid, "%d", pid);
  fprintf(stderr, "%s\n", strerror(errno));
  assert(pid_res > 0);
  fclose(fd_pid);
  
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
  jack_set_error_function(jack_error_cb);
  //  print_ports();

  /* fprintf(stderr, "Connected to JACK\nOpen /dev/input/event0"); */
  // The keyboard/pedal
  int fd;
  fd = open("/dev/input/event0", O_RDONLY);
  if(fd < 0) {
    printf("Error %s\n", strerror(errno));
    return fd;
  }

  last_yalv = 0;

  char * current_pedal = NULL;
  char A = 'A', B = 'B', C = 'C';

  /* int loop_limit = 0; */

  while(RUNNING == 1){
    /* if(loop_limit++ > 120){ */
    /*   RUNNING = 0; */
    /* } */

    tv.tv_sec = 200;
    tv.tv_usec = 0;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    retval = select(fd+1, &rfds, NULL, NULL, &tv);

    if(retval < 0){
      printf("select Error %s\n", strerror(errno));

      // TODO: What is this constant: 4?
      if(errno == 4){
	
	// Interupted by a signal
	fprintf(stderr, "signaled: %d\n", signaled);
	if(signaled){
	  destroy_pedals();
	  initialise_pedals();
	}
	signaled = 0;
	continue;
      }
      return -1;
    }else if(retval == 0){
      printf("Heartbeat...\n");
      continue;
    }
    /* fprintf(stderr, "Before IOCTL\n"); */
    memset(key_b, 0, sizeof(key_b));
    if(ioctl(fd, EVIOCGKEY(sizeof(key_b)), key_b) == -1){
      printf("IOCTL Error %s\n", strerror(errno));
      return -1;
    }
    /* fprintf(stderr, "IOCTL returnes\n"); */
    
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

	  /* fprintf(stderr, "calling deimplement(%c)\n", old_pedal ? *old_pedal : 0); */
	  deimplement_pedal(old_pedal);

	  gettimeofday(&b, NULL);

	  implement_pedal(current_pedal);

	  gettimeofday(&c, NULL);

	  fprintf(stderr, "Implement %c: %ld\n", *current_pedal, ((c.tv_sec - a.tv_sec) * 1000000) +
		 (c.tv_usec - a.tv_usec));
	  /* fprintf(stderr, "Deimplement %c: %ld\n", old_pedal?*old_pedal:'-', ((c.tv_sec - b.tv_sec) * 1000000) + */
	  /* 	 (c.tv_usec - b.tv_usec)); */
	  /* printf("Total: %ld\n", ((c.tv_sec - a.tv_sec) * 1000000) + */
	  /* 	 (c.tv_usec - a.tv_usec)); */

	  
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
  fprintf(stderr, "After main loop.  RUNNING: %d\n", RUNNING);
  return 0;
}
