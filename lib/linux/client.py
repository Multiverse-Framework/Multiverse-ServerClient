import multiverse_client_pybind as mc

# Create the pybind client
client = mc.MultiverseClientPybind()

# Set up callbacks — these are Python functions that the C++ side will call
def init_objects(from_request_meta_data: bool) -> bool:
    print("[py] init_objects:", from_request_meta_data)
    return True  # must return bool

def bind_request_meta_data():
    print("[Python] bind_request_meta_data called")
    # Here you’d prepare JSON or metadata that the server expects
    client.set_request_meta_data('{"op": "echo", "dtype": "u8", "count": 4}')

def bind_send_data():
    print("[Python] bind_send_data called")
    # Fill some data for sending
    client.set_send_data([1, 2, 3, 4])  # just an example list

def bind_receive_data():
    print("[Python] bind_receive_data called")
    data = client.get_receive_data()
    print("[Python] Received:", data)

def reset():
    print("[Python] reset called")

# Register callbacks
# client.set_init_objects_callback(init_objects)
client.set_bind_request_meta_data_callback(bind_request_meta_data)
client.set_bind_send_data_callback(bind_send_data)
client.set_bind_receive_data_callback(bind_receive_data)
client.set_reset_callback(reset)

# Connect to your server
client.connect("127.0.0.1", "9000", "9000")

# Start client threads (will call into your callbacks)
client.start()

# Communicate with the server once
ok = client.communicate(False)
print("communicate() returned:", ok)

# Read world time (exposed pointer from C++ side)
print("World time:", client.get_world_time())

# Get response meta-data string
print("Response meta-data:", client.get_response_meta_data())

# Disconnect cleanly
client.disconnect()
