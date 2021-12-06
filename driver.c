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
    fprintf(stderr, "%s -> %s\n\t", jcp->ports[0], jcp->ports[1]);
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
void _destroy_pedal(struct pedal_config * pc){
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

  fprintf(stderr, "add pedal %c: %s -> %s\n", pedal, jc1, jc2);
  pc->n_connections++;
  pc->connections =
    realloc(pc->connections, pc->n_connections * (sizeof (struct jack_connection)));
  assert(pc->connections);

  pc->connections[pc->n_connections - 1].ports[0] =
    malloc(sizeof (char)  * (1 + strlen(jc1)));

  pc->connections[pc->n_connections - 1].ports[1] =
    malloc(sizeof (char)  * (1 + strlen(jc2)));

  strcpy(pc->connections[pc->n_connections - 1].ports[0], jc1);
  strcpy(pc->connections[pc->n_connections - 1].ports[1], jc2);

}

void implement_pedal(char * pedal){
  if ( pedal == NULL ) {
    return;
  }
  print_pedal(*pedal);

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
    int r = jack_connect(CLIENT, src_port, dst_port);
    assert(!r || r == EEXIST); 
    fprintf(stderr, "C %c %s %s\n", *pedal, src_port, dst_port);
  }
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
    char * src_port = pc->connections[i].ports[0];
    char * dst_port = pc->connections[i].ports[1];
    fprintf(stderr, "D %c %s %s\n", *pedal, src_port, dst_port);
    int r = jack_disconnect(CLIENT, src_port, dst_port);  
    assert(!r || r == EEXIST); 
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

static void signal_handler(int sig)
{
  destroy_pedals();
  initialise_pedals();
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

  return 0;
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
  // Signal handler to quit programme.  Not necessary, strictly speaking
  signal(SIGHUP,signal_handler);

  // Initialise the definitions of pedals
  // Signal with HUP to change
  initialise_pedals();
  
  mi_root = getenv("PATH_MI_ROOT");
  assert(snprintf(home_dir, PATH_MAX, "%s", mi_root) <= PATH_MAX);
  chdir(home_dir);

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
  while(1){
    tv.tv_sec = 200;
    tv.tv_usec = 0;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    retval = select(fd+1, &rfds, NULL, NULL, &tv);

    if(retval < 0){
      printf("select Error %s\n", strerror(errno));
      return -1;
    }else if(retval == 0){
      printf("Heartbeat...\n");
      continue;
    }
    memset(key_b, 0, sizeof(key_b));
    if(ioctl(fd, EVIOCGKEY(sizeof(key_b)), key_b) == -1){
      printf("IOCTL Error %s\n", strerror(errno));
      return -1;
    }
    
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
	  implement_pedal(current_pedal);
	  gettimeofday(&b, NULL);
	  deimplement_pedal(old_pedal);
	  gettimeofday(&c, NULL);

	  printf("Implement %c: %ld\n", *current_pedal, ((b.tv_sec - a.tv_sec) * 1000000) +
		 (b.tv_usec - a.tv_usec));
	  printf("Deimplement %c: %ld\n", old_pedal?*old_pedal:'-', ((c.tv_sec - b.tv_sec) * 1000000) +
		 (c.tv_usec - b.tv_usec));
	  printf("Total: %ld\n", ((c.tv_sec - a.tv_sec) * 1000000) +
		 (c.tv_usec - a.tv_usec));

	  
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
  
  return 0;
}
