#!/usr/bin/env python3
"""
CHB-BLDC2418 Motor Test Client
ESP32-S3 Motor Control Test Tool

Usage:
    python motor_test_client.py --help
    python motor_test_client.py single --motor 0 --speed 225 --duration 5
    python motor_test_client.py all --speed 225 --duration 10
    python motor_test_client.py sequence --motor 0
"""

import argparse
import paho.mqtt.client as mqtt
import time
import json
import sys
from datetime import datetime

# MQTT Configuration
MQTT_BROKER = "192.168.110.31"
MQTT_PORT = 1883
MQTT_CONTROL_TOPIC = "esp32_1/control"
MQTT_DATA_TOPIC = "esp32_1/data"

# Motor Specifications
MAX_SPEED = 450  # 4500 RPM = 450 PCNT counts per second
PWM_FREQ = 20000  # 20KHz

class MotorTester:
    def __init__(self, broker=MQTT_BROKER, port=MQTT_PORT):
        self.broker = broker
        self.port = port
        self.client = mqtt.Client(client_id=f"motor_test_{int(time.time())}")
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.connected = False
        self.received_data = []
        
    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print(f"✓ Connected to MQTT Broker: {self.broker}:{self.port}")
            self.client.subscribe(MQTT_DATA_TOPIC)
            print(f"✓ Subscribed to: {MQTT_DATA_TOPIC}")
            self.connected = True
        else:
            print(f"✗ Connection failed with code: {rc}")
            
    def _on_message(self, client, userdata, msg):
        payload = msg.payload.decode('utf-8')
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        print(f"[{timestamp}] DATA: {payload}")
        self.received_data.append({
            'time': timestamp,
            'data': payload
        })
        
    def connect(self):
        """Connect to MQTT broker"""
        try:
            print(f"Connecting to {self.broker}:{self.port}...")
            self.client.connect(self.broker, self.port, 60)
            self.client.loop_start()
            time.sleep(1)  # Wait for connection
            return self.connected
        except Exception as e:
            print(f"✗ Connection error: {e}")
            return False
            
    def disconnect(self):
        """Disconnect from broker"""
        self.client.loop_stop()
        self.client.disconnect()
        print("✓ Disconnected from MQTT broker")
        
    def send_command(self, motor_idx, speed, duration):
        """Send motor control command"""
        cmd = f"cmd_{motor_idx}_{speed}_{duration}"
        print(f"\n>>> Sending: {cmd}")
        self.client.publish(MQTT_CONTROL_TOPIC, cmd)
        
    def test_single_motor(self, motor_idx, speed, duration, wait_response=True):
        """Test single motor"""
        print(f"\n{'='*60}")
        print(f"Single Motor Test - Motor {motor_idx}")
        print(f"Speed: {speed}/{MAX_SPEED} ({speed/MAX_SPEED*100:.1f}%)")
        print(f"Duration: {duration}s")
        print(f"{'='*60}")
        
        # Clear previous data
        self.received_data = []
        
        # Send command
        self.send_command(motor_idx, speed, duration)
        
        if wait_response:
            print(f"Waiting {duration + 2}s for motor operation...")
            time.sleep(duration + 2)
            
            # Analyze results
            self._analyze_results(motor_idx, speed)
            
    def test_all_motors(self, speed, duration):
        """Test all 4 motors simultaneously"""
        print(f"\n{'='*60}")
        print(f"All Motors Test - Parallel Operation")
        print(f"Speed: {speed}/{MAX_SPEED} ({speed/MAX_SPEED*100:.1f}%)")
        print(f"Duration: {duration}s")
        print(f"{'='*60}")
        
        self.received_data = []
        
        # Start all motors
        for i in range(4):
            self.send_command(i, speed, duration)
            time.sleep(0.1)  # Small delay between commands
            
        print(f"Waiting {duration + 2}s for motor operation...")
        time.sleep(duration + 2)
        
        # Analyze results for each motor
        for i in range(4):
            self._analyze_results(i, speed)
            
    def test_speed_sequence(self, motor_idx):
        """Test speed sequence: 0 -> 25% -> 50% -> 75% -> 100% -> 0"""
        speeds = [0, 112, 225, 337, 450, 0]
        duration = 5
        
        print(f"\n{'='*60}")
        print(f"Speed Sequence Test - Motor {motor_idx}")
        print(f"Sequence: {speeds}")
        print(f"{'='*60}")
        
        for speed in speeds:
            self.test_single_motor(motor_idx, speed, duration)
            time.sleep(1)
            
    def _analyze_results(self, motor_idx, expected_speed):
        """Analyze test results"""
        motor_data = [d for d in self.received_data 
                     if f"pcnt_count_{motor_idx}_" in d['data']]
        
        if not motor_data:
            print(f"⚠ No PCNT data received for Motor {motor_idx}")
            return
            
        # Extract PCNT values
        pcnt_values = []
        for d in motor_data:
            try:
                parts = d['data'].split('_')
                if len(parts) >= 3:
                    pcnt = int(parts[3])
                    pcnt_values.append(pcnt)
            except:
                pass
                
        if pcnt_values:
            avg_pcnt = sum(pcnt_values) / len(pcnt_values)
            max_pcnt = max(pcnt_values)
            min_pcnt = min(pcnt_values)
            
            error = abs(avg_pcnt - expected_speed) / expected_speed * 100 if expected_speed > 0 else 0
            
            print(f"\n📊 Motor {motor_idx} Results:")
            print(f"   Expected Speed: {expected_speed}")
            print(f"   Average PCNT:   {avg_pcnt:.1f}")
            print(f"   Min/Max PCNT:   {min_pcnt}/{max_pcnt}")
            print(f"   Error:          {error:.1f}%")
            print(f"   Samples:        {len(pcnt_values)}")
            
            if error < 10:
                print(f"   ✅ PASSED (Error < 10%)")
            else:
                print(f"   ❌ FAILED (Error >= 10%)")
        else:
            print(f"⚠ Could not parse PCNT values for Motor {motor_idx}")

def main():
    parser = argparse.ArgumentParser(
        description='CHB-BLDC2418 Motor Test Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Test single motor at 50% speed for 5 seconds
  python motor_test_client.py single -m 0 -s 225 -d 5
  
  # Test all motors at 30% speed for 10 seconds
  python motor_test_client.py all -s 135 -d 10
  
  # Run speed sequence on motor 0
  python motor_test_client.py sequence -m 0
  
  # Quick test all motors
  python motor_test_client.py quick
        """
    )
    
    subparsers = parser.add_subparsers(dest='command', help='Test command')
    
    # Single motor test
    single_parser = subparsers.add_parser('single', help='Test single motor')
    single_parser.add_argument('-m', '--motor', type=int, default=0, 
                               choices=range(4), help='Motor index (0-3)')
    single_parser.add_argument('-s', '--speed', type=int, default=225,
                               help=f'Speed value (0-{MAX_SPEED}, default: 225=50%)')
    single_parser.add_argument('-d', '--duration', type=int, default=5,
                               help='Duration in seconds (default: 5)')
    
    # All motors test
    all_parser = subparsers.add_parser('all', help='Test all motors')
    all_parser.add_argument('-s', '--speed', type=int, default=225,
                            help=f'Speed value (0-{MAX_SPEED}, default: 225=50%)')
    all_parser.add_argument('-d', '--duration', type=int, default=5,
                            help='Duration in seconds (default: 5)')
    
    # Speed sequence test
    seq_parser = subparsers.add_parser('sequence', help='Run speed sequence test')
    seq_parser.add_argument('-m', '--motor', type=int, default=0,
                            choices=range(4), help='Motor index (0-3)')
    
    # Quick test
    subparsers.add_parser('quick', help='Quick test all motors (225, 5s each)')
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        sys.exit(1)
        
    # Create tester
    tester = MotorTester()
    
    if not tester.connect():
        print("Failed to connect to MQTT broker!")
        sys.exit(1)
        
    try:
        if args.command == 'single':
            tester.test_single_motor(args.motor, args.speed, args.duration)
            
        elif args.command == 'all':
            tester.test_all_motors(args.speed, args.duration)
            
        elif args.command == 'sequence':
            tester.test_speed_sequence(args.motor)
            
        elif args.command == 'quick':
            print("\n🚀 Quick Test: All motors at 50% speed for 5s")
            tester.test_all_motors(225, 5)
            
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
    finally:
        tester.disconnect()
        print("\n✓ Test completed")

if __name__ == "__main__":
    main()
