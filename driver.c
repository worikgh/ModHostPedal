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

/* Tests if the `bit`th bit is set in `array.  Used to detect pedaal
   depressions` */
int test_bit(unsigned bit, uint8_t *array)
{
  return (array[bit / 8] & (1 << (bit % 8))) != 0;
}

/*
  Jack stuff
 */
jack_client_t *client;
static void signal_handler(int sig)
{
  jack_client_close(client);
  fprintf(stderr, "signal %d received, exiting ...\n", sig);
  exit(0);
}
void
jack_shutdown (void *arg)
{
  exit (1);
}

/* Pedals are defined by jack plumbing.  Each line in a pedal file is
   the deinition (src/sink) of a jack pipe.  Thi ssets up those
   pipes */
void process_line(char * line){
  const char * src_port, * dst_port;
  int r;
  char * tok = strtok(line, " ");
  src_port = tok;
  assert(src_port);
  dst_port = strtok(NULL, " ");
  assert(dst_port);
  r = jack_connect(client, src_port, dst_port);
  assert(!r || r == EEXIST); 
}

/* Disconnect jack pipes to stdin and stdout so the pedal can replace
   them */
void clear_jack(){
  int i;
  const char ** ports = jack_get_ports(client, "system",
				 "32 bit float mono audio", 0 );
  for(i = 0; ports[i]; i++){
    /* printf("Port: %s\n", ports[i]); */
    jack_port_disconnect(client, jack_port_by_name(client, ports[i]));
  }
}

/* Called when a pedal is depressed. The argument defines the pedal */
int load_pedal(char p){
  int i;
  FILE * fd;
  char  scriptname[PATH_MAX];

  const char * path_mi_root;
  char ch;
  struct timeval a, b;
  /* We do not want buffer overruns... */
  const uint LINE_MAX = 1024;
  char line[LINE_MAX - 1];

  path_mi_root = getenv("PATH_MI_ROOT");
  
  gettimeofday(&a, NULL);
  
  switch (p){
  case 'A':
    sprintf(scriptname, "%s/pedal_driver/A", path_mi_root);
    break;
  case 'B':
    sprintf(scriptname, "%s/pedal_driver/B", path_mi_root);
    break;
  case 'C':
    sprintf(scriptname, "%s/pedal_driver/C", path_mi_root);
    break;
  default:
    break;
  }

  /* Clear the Jack input and output */
  clear_jack();
  fd = fopen(scriptname, "r");
  assert(fd);
  i = 0;
  while((ch = fgetc(fd)) != EOF){  /* while(!feof(fd)){ */
    assert(i < LINE_MAX);// We really do not want buffer over runs. 
    line[i] = ch;
    if((ch >= 'a'  && ch <= 'z') ||
       (ch >= 'A'  && ch <= 'Z') ||
       (ch >= '0' && ch <= '9') ||
       ch == '_' || ch == ' ' || ch == ':' || ch == '\n'){
      if(line[i] == '\n'){
	line[i] = '\0';
	process_line(line);
	i = 0;
      }else{
	i++;
      }
    }else{
      break;
    }
  }
  gettimeofday(&b, NULL);
  printf("Took %ld microseconds for %s\n",
	 ((b.tv_sec - a.tv_sec) * 1000000) +
	 (b.tv_usec - a.tv_usec), scriptname); 
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
  
  mi_root = getenv("PATH_MI_ROOT");
  assert(snprintf(home_dir, PATH_MAX, "%s/pedal_driver", mi_root) <= PATH_MAX);
  chdir(home_dir);

  printf("Driver getting started in %s\n", home_dir);

  /* Set up the client for jack */
  client = jack_client_open ("client_name", JackNullOption, &status);
  if (client == NULL) {
    fprintf (stderr, "jack_client_open() failed, "
	     "status = 0x%2.0x\n", status);
    if (status & JackServerFailed) {
      fprintf (stderr, "Unable to connect to JACK server\n");
    }
    exit (1);
  }

  int fd;
  fd = open("/dev/input/event0", O_RDONLY);
  if(fd < 0) {
    printf("Error %s\n", strerror(errno));
    return fd;
  }

  last_yalv = 0;
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
	  printf("Got key\n");
	  /* Only when it changes */
	  if(yalv == 0x1e){
	    /* Pedal A */
	    load_pedal('A');
	  }else if(yalv == 0x30){
	    /* Pedal B */
	    load_pedal('B');
	  }else if(yalv == 0x2e){
	    /* Pedal C */
	    load_pedal('C');
	  }	    
	  last_yalv = yalv;
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
