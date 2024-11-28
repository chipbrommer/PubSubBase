import zmq
import struct

class Msg:
    # Define the format for struct unpacking with standard size (no padding)
    # 'BBIB' means:
    # B: uint8_t (1 byte)
    # B: uint8_t (1 byte)
    # I: uint32_t (4 bytes)
    # B: uint8_t (1 byte)
    _format = '=BBIB'  # Use '=' to avoid padding
    _size = struct.calcsize(_format)

    def __init__(self, sync1=0x01, sync2=0x02, counter=0, eob=0x03):
        self.sync1 = sync1
        self.sync2 = sync2
        self.counter = counter
        self.eob = eob

    @classmethod
    def from_bytes(cls, byte_data):
        """Unpacks byte data into a Msg instance."""
        if len(byte_data) != cls._size:
            raise ValueError(f"Invalid byte data size. Expected {cls._size}, got {len(byte_data)}.")
        sync1, sync2, counter, eob = struct.unpack(cls._format, byte_data)
        return cls(sync1, sync2, counter, eob)

    def to_bytes(self):
        """Packs the Msg instance into a byte array."""
        return struct.pack(self._format, self.sync1, self.sync2, self.counter, self.eob)

    def __repr__(self):
        """Custom string representation."""
        return f"Msg(sync1={self.sync1}, sync2={self.sync2}, counter={self.counter}, eob={self.eob})"


def zmq_subscriber():
    # Create a ZeroMQ context
    context = zmq.Context()

    # Create a SUB socket
    subscriber = context.socket(zmq.SUB)

    # Connect to the tcp endpoint
    subscriber.connect("tcp://127.0.0.1:5555")

    # Subscribe to the 'test' topic
    subscriber.setsockopt_string(zmq.SUBSCRIBE, "test")

    print("Subscriber connected and listening to 'test' topic...")

    try:
        while True:
            # Receive a message (byte array)
            topic = subscriber.recv_string()
            message = subscriber.recv()

            # Convert byte data to Msg instance
            try:
                msg = Msg.from_bytes(message)

                # Verify the message structure
                if msg.sync1 == 0x01 and msg.sync2 == 0x02 and msg.eob == 0x03:
                    print(f"Valid message received: Counter = {msg.counter}")
                else:
                    print(f"Invalid message structure detected: sync1={msg.sync1}, sync2={msg.sync2}, eob={msg.eob}")
            except ValueError as e:
                print(f"Error unpacking message: {e}")

    except KeyboardInterrupt:
        print("\nSubscriber shutting down.")

    finally:
        # Clean up
        subscriber.close()
        context.term()


if __name__ == "__main__":
    zmq_subscriber()
