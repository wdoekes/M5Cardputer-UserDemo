"""
Daemon that listens for JSON over UDP (port 5005 to broadcast).

EXAMPLE

  Example packet:

    x.x.x.x -> 255.255.255.255: {"code":5,"btn":"b","row":3,"col":7,"state":1}

  Should be read as keycode 5 state PRESSED.

  The CardputerADV might have an application that broadcasts these.
  Walter knows.

  With this, you can broadcast: b,9 to send 100% close command to the window
  blinds. or b,0 for 0% closed (100% open).

  Right now, 'b' and 'c' are implemented (blinds and chill-blinds). They take
  0..9 as argument, where 9 is 100% closed, 4 is 50% and 0 is 0% closed.

SECURITY

  There is 0 security here. But the commands that it can execute are limited.
  Worst case you're flooding the homeassistant with UP/DOWN calls to the window
  blinds. That might cause them to overheat and/or die.

  We could build some kind of rate-limiting. But on the other hand, on this
  private network there shouldn't be malicious actors doing UDP.
"""
import asyncio
import json
import time

import httpx

from wifikbd_settings import HOMEASSISTANT_API_URL, HOMEASSISTANT_TOKEN

# Configuration
UDP_IP = "0.0.0.0"
UDP_PORT = 5005
EXPIRY_TIME = 10.0  # Seconds to keep partial sequences

COVER_OVERSHOOT = 3  # 2 is _almost_ enough


class CardputerLink:
    def __init__(self):
        # Stores keys per sender IP:
        # {'192.168.2.46': [(scancode, timestamp), ...]}
        self.buffer = {}
        # Tracks long running tasks per entity:
        # {entity_id: Task}
        self.active_tasks = {}

    def cleanup(self):
        """Removes keypress sequences older than EXPIRY_TIME"""
        now = time.time()
        for ip in list(self.buffer.keys()):
            # Truncate always.
            self.buffer[ip] = self.buffer[ip][-32:]  # max 32 entries
            # Check the last entry (latest timestamp).
            scancode, timestamp = self.buffer[ip][-1]
            if now - timestamp > EXPIRY_TIME:
                del self.buffer[ip]

    def handle_keypress(self, source_ip, key_name):
        if source_ip not in self.buffer:
            self.buffer[source_ip] = []

        self.buffer[source_ip].append((key_name, time.time()))

        # One of 0..9 ends a sequence.
        if key_name in '0123456789':
            sequence = [kp[0] for kp in self.buffer[source_ip]]
            # We move the async spawning here
            asyncio.create_task(self.process_command(sequence, source_ip))
            del self.buffer[source_ip]

    async def process_command(self, sequence, addr):
        if len(sequence) < 2:
            return

        print(f"[I] Processing {sequence} from {addr}")
        device_letter = sequence[-2]    # 'a' or 'z' or 'del' or ...
        digit = int(sequence[-1])       # '0' or '1' or ...

        # 0=0% closed, 1=20% closed, 2=30% closed, ..., 9=100% closed
        closed_pct = ((digit + 1) * 10) if digit > 0 else 0
        target_pos = 100 - closed_pct

        entity_id = None
        if device_letter == 'b':
            entity_id = "cover.windowblinds_cover_0"
        elif device_letter == 'c':
            entity_id = "cover.windowblinds_cover_1"

        if entity_id:
            # If a task is already running for this blind, kill it.
            if entity_id in self.active_tasks:
                self.active_tasks[entity_id].cancel()
                print(f"[!] Cancelled previous task for {entity_id}")

            # Start new task and store it.
            task = asyncio.create_task(
                self.call_ha_blinds(entity_id, target_pos))
            self.active_tasks[entity_id] = task

            # Clean up the task map when done.
            task.add_done_callback(
                lambda t: self.active_tasks.pop(entity_id, None))

    async def call_ha_blinds(self, entity_id, target_pos):
        headers = {
            "Authorization": f"Bearer {HOMEASSISTANT_TOKEN}",
            "Content-Type": "application/json",
        }
        base_url = HOMEASSISTANT_API_URL

        async with httpx.AsyncClient() as client:
            try:
                # Get current state
                resp = await client.get(
                    f"{base_url}/states/{entity_id}", headers=headers)
                current_pos = (
                    resp.json().get('attributes', {})
                    .get('current_position', 0))

                # Overshoot logic:
                # - if the blinds are going up (target_pos higher)
                # - then we want to go even higher
                # - and then go back a bit.
                # This causes the slats to return to the down/closed angle.
                # - If target_pos is equal then also do overshoot handling
                #   maybe it fixes a poor command sequence earlier.
                if (target_pos >= current_pos
                        and (target_pos + COVER_OVERSHOOT) <= 100):
                    overshoot = target_pos + COVER_OVERSHOOT
                    await client.post(
                        f"{base_url}/services/cover/set_cover_position",
                        json={"entity_id": entity_id, "position": overshoot},
                        headers=headers)

                    # Polling until we're at the overshoot position.
                    print(
                        f"[*] Moving {entity_id} to overshoot {overshoot}% "
                        f"open to be able to flip slats")
                    poll_wait = 0.6
                    for _ in range(int(45 / poll_wait)):
                        await asyncio.sleep(poll_wait)
                        p_resp = await client.get(
                            f"{base_url}/states/{entity_id}", headers=headers)
                        state = p_resp.json()
                        actual = (  # is 0% until it's the final value
                            state.get('attributes', {})
                            .get('current_position'))
                        moving = state.get('state') in ('opening', 'closing')
                        if actual == overshoot or not moving:
                            break

                # Final target.
                print(f"[*] Moving {entity_id} to {target_pos}% open")
                await client.post(
                    f"{base_url}/services/cover/set_cover_position",
                    json={"entity_id": entity_id, "position": target_pos},
                    headers=headers)
            except asyncio.CancelledError:
                print(
                    f"[-] Task for {entity_id} was cancelled "
                    f"by a newer command.")
            except Exception as e:
                print(f"[!] Error: {e}")


class UDPProtocol(asyncio.DatagramProtocol):
    def __init__(self, link_logic):
        self.link = link_logic

    def datagram_received(self, data, addr):
        try:
            # Decode the JSON packet:
            # {"code":5,"btn":"b","row":3,"col":7,"state":1}
            udata = data.decode().strip('\x00')
            print(f"[D] Got {udata} from {addr}")
            payload = json.loads(udata)
            btn = payload.get("btn")
            state = payload.get("state")

            # We only care about Key Down (s: 1) to avoid double triggers
            if state == 1:
                ip = addr[0]
                self.link.handle_keypress(ip, btn)

        except Exception as e:
            print(f"[!] Error parsing packet: {e}")


async def cleanup_task(link_logic):
    """Background loop to clear stuck keypresses"""
    while True:
        await asyncio.sleep(5)
        link_logic.cleanup()


async def main():
    link_logic = CardputerLink()
    loop = asyncio.get_running_loop()

    print(f"[*] Starting UDP server on {UDP_IP}:{UDP_PORT}...")

    # Start the transport
    transport, protocol = await loop.create_datagram_endpoint(
        lambda: UDPProtocol(link_logic),
        local_addr=(UDP_IP, UDP_PORT)
    )

    # Start the cleanup worker
    asyncio.create_task(cleanup_task(link_logic))

    try:
        await asyncio.Future()  # Run forever
    finally:
        transport.close()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[*] Shutting down.")
