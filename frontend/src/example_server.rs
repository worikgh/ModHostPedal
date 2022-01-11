extern crate websocket;

use std::str;
use std::sync::{Arc, Mutex};
use std::sync::mpsc;
use std::thread;

use websocket::{Server, Message, Sender, Receiver};
use websocket::header::WebSocketProtocol;
use websocket::message::Type;


fn main() {
    let server = Server::bind("0.0.0.0:2794").unwrap();

    let (dispatcher_tx, dispatcher_rx) = mpsc::channel::<String>();
    let client_senders: Arc<Mutex<Vec<mpsc::Sender<String>>>> = Arc::new(Mutex::new(vec![]));

    // dispatcher thread
    {
	let client_senders = client_senders.clone();
	thread::spawn(move || {
	    while let Ok(msg) = dispatcher_rx.recv() {
		for sender in client_senders.lock().unwrap().iter() {
		    sender.send(msg.clone()).unwrap();
		}
	    }
	});
    }

    // client threads
    for connection in server {
	let dispatcher = dispatcher_tx.clone();
	let (client_tx, client_rx) = mpsc::channel();
	client_senders.lock().unwrap().push(client_tx);

	// Spawn a new thread for each connection.
	thread::spawn(move || {
	    let request = connection.unwrap().read_request().unwrap(); // Get the request
	    let headers = request.headers.clone(); // Keep the headers so we can check them

	    request.validate().unwrap(); // Validate the request

	    let mut response = request.accept(); // Form a response

	    if let Some(&WebSocketProtocol(ref protocols)) = headers.get() {
		if protocols.contains(&("rust-websocket".to_string())) {
		    // We have a protocol we want to use
		    response.headers.set(WebSocketProtocol(vec!["rust-websocket".to_string()]));
		}
	    }

	    let mut client = response.send().unwrap(); // Send the response

	    let ip = client.get_mut_sender()
		.get_mut()
		.peer_addr()
		.unwrap();

	    println!("Connection from {}", ip);

	    let message: Message = Message::text("SERVER: Connected.".to_string());
	    client.send_message(&message).unwrap();

	    let (mut sender, mut receiver) = client.split();

	    let(tx, rx) = mpsc::channel::<Message>();
	    thread::spawn(move || {
		for message in receiver.incoming_messages() {
		    tx.send(message.unwrap()).unwrap();
		}
	    });

	    loop {
		if let Ok(message) = rx.try_recv() {
		    match message.opcode {
			Type::Close => {
			    let message = Message::close();
			    sender.send_message(&message).unwrap();
			    println!("Client {} disconnected", ip);
			    return;
			},
			Type::Ping => {
			    let message = Message::pong(message.payload);
			    sender.send_message(&message).unwrap();
			},
			_ => {
			    let payload_bytes = &message.payload;
			    let payload_string = match str::from_utf8(payload_bytes) {
				Ok(v) => v,
				Err(e) => panic!("Invalid UTF-8 sequence: {}", e),
			    };
			    let msg_string = format!("MESSAGE: {}: ", payload_string);
			    dispatcher.send(msg_string).unwrap();
			}
		    }
		}
		if let Ok(message) = client_rx.try_recv() {
		    let message: Message = Message::text(message);
		    sender.send_message(&message).unwrap();
		}
	    }
	});
    }
}
