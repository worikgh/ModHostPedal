// #[macro_use]
extern crate log;

extern crate ncurses;
extern crate simplelog;

use log::info; //, trace, warn};
use ncurses::*;
use simplelog::*;

use nix;
use nix::fcntl::FlockArg;
use std::collections::HashMap;
use std::env;
use std::fs;
use std::fs::File;
use std::io::prelude::*;
use std::io::Read;
#[cfg(unix)]
use std::os::unix::io::{AsRawFd, RawFd};
#[cfg(target_os = "wasi")]
use std::os::wasi::io::{AsRawFd, RawFd};
use std::path::Path;
use std::process;
use std::process::Stdio;
use std::sync::mpsc;
use std::sync::Arc;
use std::sync::Mutex;
use std::thread;
use std::time;

//use std::time::Instant;
// https://docs.rs/ws/
use ws;

// Message formats for ws Server and Client Message
mod shared;

// ---------------------------------------------
// Websocket server code starts
#[derive(Copy, Clone, Debug)]
struct PedalState {
    // `state` is to be set by a pedal.  The pedal available has three
    // pedals, that operate via USB port and look like a keyboard with
    // three keys: 'A', 'B', 'C'.  Changnig `state` will change the
    // LV2 effects loaded into the signal path.
    state: u8,
}

/// `ServerState` is used by the Server to do its (non-ws) jobs.  It
/// needs to be accessible to the `Handler` instances so they can
/// initialise clients and respond to the client requests.
/// `ServerState` stores the available pedal boards in a Hash String =>
/// String of names of pedals, and the current pedal board as
/// String
#[derive(Clone, Debug)]
struct ServerState {
    selected_pedal_board: String,

    // Map pedal board name to the list of pedals (created by Modep
    // and organised by `preocess_modep.pl`).  The list of boards is a
    // space delimited ready for `setpedals`
    pedal_boards: HashMap<String, String>,

    // A transmitter for each `Handle` so changes in state can be
    // propagated to the `WSHandler` objects, and thus to the clients.
    // It is here so WSHandler objects can delete themselves
    txs: HashMap<usize, mpsc::Sender<PedalState>>,
}

impl ServerState {
    fn new() -> Self {
        // if pedal_boards.len() == 0 {
        //     panic!("No pedal boards in list");
        // }

        // info!(
        //     "Pedal Boards: {}",
        //     pedal_boards
        //         .iter()
        //         .fold(String::new(), |a, b| format!("{} {} {}", a, b.0, b.1))
        // );

        // let dir = PathBuf::from(dir.as_str());
        // if !dir.is_dir() {
        //     panic!("{} is not a directory!", dir.to_str().unwrap());
        // }

        // // Get essentially a random pedal board to have active at first
        // let selected_pedal_board = String::from(
        //     pedal_boards
        //         .get(pedal_boards.keys().nth(0).unwrap())
        //         .unwrap(),
        // );
        // let mut pedal_boards = HashMap::new();

        // for entry in dir.read_dir().expect("read_dir call failed") {
        //     if let Ok(entry) = entry {
        //         // entry is std::fs::DirEntry
        //         let p = entry.path();

        //         // Files with no extension are descriptions of settings
        //         // for the instrument so find them
        //         if p.as_path().is_file() && p.as_path().extension().is_none() {
        //             // Just the name, extraced from the path
        //             let instrument_name =
        //                 p.file_name().unwrap().to_str().unwrap().to_string();

        //             // If the instrument name is in the list of
        //             // pedal boards to present to the user then store
        //             // it in the vector
        //             if pedal_boards.contains_key(&instrument_name) {
        //                 pedal_boards.insert(
        //                     instrument_name.clone(),
        //                     p.to_str().unwrap().to_string(),
        //                 );
        //             } else {
        //             }
        //         }
        //     }
        // }
        let pedal_boards: HashMap<String, String> = load_pedal_boards();
        let selected_pedal_board: String =
            pedal_boards.keys().nth(0).unwrap().to_string();
        ServerState {
            selected_pedal_board: selected_pedal_board,
            pedal_boards: pedal_boards,
            txs: HashMap::new(),
        }
    }
}
/// Web Socket Server code: Two main classes: WSFactory, and
/// WSHandler.  The WSFactory is a singleton.  It runs the code that
/// listens on the socked for client connections (see `client.rs`) and
/// for each connection it builds a WSHandler.

/// There is two way communication between the server and the clients.
/// The clients issue commands to change the pedal board in use, the
/// server arranges for it to change then it notifies the clients
/// about the new settings.

/// The server also listens to a "pedal".  (FIXME Does it??) It sends
/// single character commands from a limited set of characters to say
/// which pedal is depressed.  Then the server aranges for quick
/// (<10ms ideally) changes in audio routing from the STDIN, then
/// notifies the clients so they can display the state to the person
/// using the system.  This is to operate real time changes in the
/// effect chain.  Currently only from STDIN so for guitar efects....

struct WSFactory {
    // The state of the server.  It is shared state, shared with the
    // `WSHandler` objects.  They can change it
    server_state: Arc<Mutex<ServerState>>,
}

struct WSHandler {
    // For communicating with clients
    out: ws::Sender,

    // Link to state
    server_state: Arc<Mutex<ServerState>>,
}

impl WSHandler {
    /// Send the client the information it needs to get started.
    fn init_for_client(&mut self) -> String {
        let server_state = self.server_state.lock().unwrap();
        let mut ret = format!("{}\n", server_state.selected_pedal_board);

        // Send a string with the names of the pedal boards
        for (x, _) in server_state.pedal_boards.iter() {
            ret = format!("{} {}", ret, x);
        }
        ret += "\n";
        ret
    }

    /// `WSHandler` is constructed with three arguments: (1) The
    /// communication channel with clients (2) The channel to get
    /// messages from the server (3) Shared state.  Shared with all
    /// other `WSHandler` objects and the `WSFactory` object that runs
    /// the show
    fn new(
        // Talk to clients
        out: ws::Sender,

        // Get messages from server to send to clients
        rx: mpsc::Receiver<PedalState>,

        // Read and adjust the servers state
        server_state: Arc<Mutex<ServerState>>,
    ) -> Self {
        let mut ret = Self {
            out: out,
            server_state: server_state,
        };

        // `run` spawns a thread and runs in background broadcasting
        // changes in state to clients
        ret.run(rx);
        info!("New handler: {:?}", &ret.out.token());
        ret
    }

    /// `run` spawns a thread to listen for pedal state changes from
    /// `WSFactory`.  Then pass it on to the clients so they can
    /// update their displays
    fn run(&mut self, rx: mpsc::Receiver<PedalState>) {
        // Listem for state updates

        info!("WSHandler::run");
        let out_t = self.out.clone();
        thread::spawn(move || {
            loop {
                let state = match rx.recv() {
                    // Got sent new state information to propagate
                    Ok(s) => s,

                    Err(err) => {
                        info!("WSHandler rx.recv() error: {:?}", err);
                        break;
                    }
                };

                // This is the message for the client
                let content = format!("PEDALSTATE {}", state.state);
		info!("From client: {}", content);
                let message = shared::ServerMessage {
                    id: out_t.token().into(),
                    text: content,
                };

                info!("sending message: {:?}", message);

                let token: usize = out_t.token().into();
                info!("WSHandler::run: send_message {:?} to {}", &message, &token);
                let server_msg: ws::Message =
                    serde_json::to_string(&message).unwrap().into();
                match out_t.broadcast(server_msg) {
                    Ok(_) => (),
                    Err(err) => panic!("{}", err),
                };
            }
        });
    }
}

impl WSFactory {
    fn new() -> Self {
        let ret = Self {
            // Initialise available pedal boards and select one to be
            // current.  That is what server_state is
            server_state: Arc::new(Mutex::new(ServerState::new())),
        };
        ret
    }

    /// Spawn a thread to listen for pedal changes on the rx.  When
    /// one is received send it to all the clients so they can update
    /// their displays
    fn run(
        &mut self,
        rx: mpsc::Receiver<PedalState>,
    ) -> Option<thread::JoinHandle<()>> {
        info!("WSFactory::run");

        // Copy of the transmiters to send data to each Handler
        let s_state = self.server_state.clone(); //lock().unwrap().txs.clone();

        Some(thread::spawn(move || loop {
            let state = match rx.recv() {
                Ok(s) => s,
                Err(err) => {
                    info!("Sender has hung up: {}", err);
                    break;
                }
            };
            let arc_txs = &s_state.lock().unwrap().txs;
            info!("state: {:?} {} children", state, arc_txs.len());
            for (_, tx) in arc_txs {
                match tx.send(state) {
                    Ok(x) => info!("WSFactory sent: {:?}", x),
                    Err(e) => info!("WSFactory err: {:?}", e),
                };
            }
        }))
    }
}

impl ws::Factory for WSFactory {
    type Handler = WSHandler;

    /// When a client starts and it wants to communicate with this
    /// using websockets `connection_made(&mut self, ws::Sender)` is
    /// called
    fn connection_made(&mut self, out: ws::Sender) -> Self::Handler {
        // Get the identifier of the socket
        let token: usize = out.token().into();

        // tx will talk to the global ServerState.
        // rx will talk to handler
        let (tx, rx) = mpsc::channel();
        {
            // Put `tx` into the HashMap of mpsc::Sender in server state
            let txs = &mut self.server_state.lock().unwrap().txs;
            txs.insert(token, tx);
            info!(
                "connection_made Token: {}: txs.len(): {}",
                &token,
                txs.len()
            );
        }

        WSHandler::new(out, rx, self.server_state.clone())
    }
}

impl ws::Handler for WSHandler {
    /// Creates a new WebSocket handshake HTTP response from the
    /// `ws::Request`
    fn on_request(&mut self, req: &ws::Request) -> ws::Result<ws::Response> {
        match req.resource() {
            "/ws" => ws::Response::from_request(req),
            _ => Ok(ws::Response::new(
                200,
                "OK",
                b"Websocket server is running".to_vec(),
            )),
        }
    }

    /// Handle messages recieved in the websocket
    fn on_message(&mut self, msg: ws::Message) -> ws::Result<()> {
        let client_id: usize = self.out.token().into();

        // Only process text messages
        if !msg.is_text() {
            Err(ws::Error::new(
                ws::ErrorKind::Internal,
                "Unknown message type",
            ))
        } else {
            // `msg` is type: `ws::Message::Text(String)` The
            // contained string is JSON shared::ServerMessage
            if let ws::Message::Text(client_msg) = msg {
                info!("on_message {:?}", &client_msg);

                let client_message: shared::ClientMessage =
                    serde_json::from_str(client_msg.as_str()).unwrap();

                // Commands are separated with whitespace
                let cmds: Vec<&str> =
                    client_message.text.split_whitespace().collect();

                // Respond to message
                let response = match cmds[0] {
                    // INIT from client is asking for the
                    // information it needs to initialise its
                    // state.
                    "INIT" => {
                        let return_msg =
                            format!("INIT {}", self.init_for_client());

                        shared::ServerMessage {
                            id: client_id,
                            text: return_msg,
                        }
                    }

                    // INSTR is when a user has selected a
                    // pedal.
                    "INSTR" => {
                        // INSTR <pedal>
                        // User has selected a pedal

                        if cmds.len() > 1 {
                            let pedal_name = cmds[1];
                            let mut server_state =
                                self.server_state.lock().unwrap();

                            match server_state.pedal_boards.get(pedal_name) {
                                Some(pedal) => {
                                    set_instrument(pedal);
                                    server_state.selected_pedal_board =
                                        pedal_name.to_string();
                                }
                                None => {
                                    info!(
                                        "Cannot find pedal named: {}, ",
                                        pedal_name
                                    )
                                }
                            };
                        }

                        shared::ServerMessage {
                            id: client_id,
                            text: self
                                .server_state
                                .lock()
                                .unwrap()
                                .selected_pedal_board
                                .clone(),
                        }
                    }

                    // Echo every thing else to all connections
                    key => shared::ServerMessage {
                        id: client_id,
                        text: key.to_string(),
                    },
                };

                // Broadcast to all connections.
                send_message(response, &self.out)
            } else {
                panic!("No!")
            }
        }
    }

    fn on_close(&mut self, code: ws::CloseCode, reason: &str) {
        let client_id: usize = self.out.token().into();
        let code_number: u16 = code.into();
        info!(
            "WebSocket closing - client: {}, code: {} {:?}, reason: {}",
            client_id, code_number, code, reason
        );
        match self.server_state.lock().unwrap().txs.remove(&client_id) {
            Some(_) => info!("Removed ws client {}", client_id),
            None => info!("Failed to remove ws client {}", client_id),
        };
    }
}

/// Get the path to the PEDALS directory as a String
fn get_dir() -> String {
    // First find the directory:
    let dir = match env::var("PATH_MI_ROOT") {
        Ok(d) => format!("{}/PEDALS", d),
        Err(_) => {
            // Environment variable not set.  We used to try to find
            // it relative to the current directory, bad idea.
            panic!("Set the PATH_MI_ROOT environment variable")
        }
    };

    // Check `dir` exists and names a directory
    match fs::metadata(dir.as_str()) {
        Ok(md) => {
            if !md.file_type().is_dir() {
                panic!("{} is not a directory", dir.as_str())
            }
        }
        Err(e) => panic!("{} is not a directory: {:?}", dir.as_str(), e),
    }
    dir.to_string()
}

// Deprecated
/*
fn set_pedal(p: char) {
    info!("set_pedal {}", p);
    let now1 = Instant::now();

    // This takes a bit over 100ms.  Need to get it to 10ms.
    run_control(&ControlType::Command(format!("p {}", p)));

    let now2 = Instant::now();
    info!("Pedal done {}: {:?}", p, now2 - now1);
}
*/

// TODO: Comment this.  What is it for?
enum ControlType {
    File(String),
    Command(String),
}
/// Access the `control` binary.  This will block!  
fn run_control(command: &ControlType) {
    // Get the root directory where`control` lives
    let dir = match env::var("PATH_MI_ROOT") {
        Ok(d) => d,
        Err(_) => {
            // Environment variable not set.  We used to try to find
            // it relative to the current directory, bad idea.
            panic!("Set the PATH_MI_ROOT environment variable")
        }
    };

    let exec_name = format!("{}/control", dir);
    match command {
	ControlType::File(f) => info!("Command type: File({})", f),
	ControlType::Command(f) => info!("Command type: Command({})", f),
    };
    info!(
        "exec_name {} {}",
        exec_name,
        match command {
            ControlType::File(file_path) => format!("File {}", file_path),
            ControlType::Command(cmd) => format!("Command {}", cmd),
        }
    );
    let mut child = match command {
        ControlType::File(file_path) => {
            let mut process = process::Command::new(exec_name.as_str())
                .arg(file_path)
                .stdout(process::Stdio::piped())
                .stderr(process::Stdio::piped())
                .spawn()
                .expect("Failed");
            let mut stdout = process.stdout.take().unwrap();
            let mut output: Vec<u8> = Vec::new();
            stdout.read_to_end(&mut output).unwrap();
            info!("Control stdout: {}", String::from_utf8(output).unwrap());
            let mut stderr = process.stderr.take().unwrap();
            let mut errput: Vec<u8> = Vec::new();
            stderr.read_to_end(&mut errput).unwrap();
            info!("Control stderr: {}", String::from_utf8(errput).unwrap());
            process
        }
        ControlType::Command(cmd) => {
            let mut process = process::Command::new(exec_name.as_str())
                .stdin(Stdio::piped())
                .stdout(process::Stdio::piped())
                .stderr(process::Stdio::piped())
                .spawn()
                .expect("Failed");
            let mut stdin = process.stdin.take().unwrap();
            stdin.write_all(cmd.as_bytes()).expect("Failed to send cmd");
            process
        }
    };

    let ecode = child.wait().expect("failed to wait on child");
    let res = ecode.success();
    info!("set_instrument: res: {}", res);
    assert!(res);
}

fn set_instrument(file_path: &str) {
    info!("set_instrument: file_path {}", file_path);
    run_control(&ControlType::File(file_path.to_string()));

    // Connect VU meter if there
    run_control(&ControlType::Command("b".to_string()));

    info!("set_instrument done");
}

fn send_message(
    server_msg: shared::ServerMessage,
    out: &ws::Sender,
) -> ws::Result<()> {
    let token: usize = out.token().into();
    info!("fn send_message {:?} to {}", &server_msg, &token);
    let server_msg: ws::Message =
        serde_json::to_string(&server_msg).unwrap().into();
    out.broadcast(server_msg)
}

//fn load_pedal_boards() -> ServerState {
fn load_pedal_boards() -> HashMap<String, String> {
    // Build the list of pedal boards and select one to be
    // current. That defines a `ServerState`.

    // The pedal boards are described in PEDALS/.LIST.  Each line is:
    // <Pedal board name>: <board> <board> <board>

    // First find the directory:
    let dir = get_dir();

    // Get the list of pedal boards that are used. There can be more
    // pedal boards in the directpry than are used.
    let list_name = format!("{}/.LIST", &dir);

    info!("fn load_pedal_boards: list_name: {}", list_name);

    let mut list_d = String::new();

    File::open(list_name.as_str())
        .unwrap()
        .read_to_string(&mut list_d)
        .unwrap();

    // Collect the pedal board definitions
    //let pedal_boards: HashMap<String, String> =
    list_d
        .as_str()
        .lines()
        .filter(
            |x| {
                x
                    // This skips blank lines and lines where first
                    // non-whitespace character is '#'
                    // Split line into whitspace seperated words
                    .split_whitespace()
                    // Choose the next word.  If no words return "#".
                    // This will force blank lines to be skipped
                    .next()
                    .unwrap_or("#")
                    .bytes()
                    // Get first byte
                    .next()
                    .unwrap()
                    != b'#'
            }, 
        )
        .map(|x| {
            // Make a 2-tupple name => value
            match x.split_once(':') {
                Some((name, value)) => (name.to_string(), value.to_string()),
                None => panic!("No name on string {}", x),
            }
        })
        .collect()
}

fn main() -> std::io::Result<()> {
    CombinedLogger::init(vec![
        WriteLogger::new(
            LevelFilter::Warn,
            Config::default(),
            File::create("server.log").unwrap(),
        ),
        WriteLogger::new(
            LevelFilter::Info,
            Config::default(),
            File::create("server.log").unwrap(),
        ),
    ])
    .unwrap();
    info!("Starting server");

    // Listen on an address and call the closure for each connection

    // Create channel to communicate with the server.
    let (tx, rx) = mpsc::channel();

    // Initialise the available pedal boards.
    //   Pedal boards are described in PEDALS/.LIST
    let mut my_factory = WSFactory::new();

    let server_handle = my_factory.run(rx);
    let wss = ws::WebSocket::new(my_factory).unwrap();

    // Run the web sockets.
    let s_thread = thread::spawn(move || wss.listen("0.0.0.0:9000").unwrap());

    let mut current_pedal: u8 = 0; // The character defining the pedal (ASCII).
    let pedal_thread = thread::spawn(move || {
        initscr();

        loop {
            // Each time around the loop check if the pedal has
            // changed.  The file PEDALS/.PEDAL is maintained by the
            // pedal driver
            let current_pedal_fn = format!("{}/.PEDAL", &get_dir());
            let path = Path::new(current_pedal_fn.as_str());
            let mut file = match File::open(&path) {
                Err(why) => panic!("couldn't open {}: {}", path.display(), why),
                Ok(file) => file,
            };
            let fd: RawFd = file.as_raw_fd();
            match nix::fcntl::flock(fd, FlockArg::LockExclusive) {
                Ok(()) => (),
                Err(err) => {
                    eprintln!("Failed to lock PEDAL file: {}", err);
                    return -1;
                }
            }

            // Read the file contents into a string, returns `io::Result<usize>`
            let mut s = String::new();
            match file.read_to_string(&mut s) {
                Err(why) => panic!("couldn't read {}: {}", path.display(), why),
                Ok(_) => (),
            }

            let c: u8 = *s.into_bytes().first().unwrap();
            if c != current_pedal {
                tx.send(PedalState { state: c }).unwrap();
                current_pedal = c;
            }

            // Ten times a second, roughly, until the heat death of the universe...
            let onhundred_millis = time::Duration::from_millis(100);
            thread::sleep(onhundred_millis);
        }
    });

    server_handle.unwrap().join().unwrap();
    s_thread.join().unwrap();
    pedal_thread.join().unwrap();
    Ok(())
}
