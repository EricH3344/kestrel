import 'package:flutter/material.dart';

class DroneControlScreen extends StatefulWidget {
  const DroneControlScreen({super.key});

  @override
  State<DroneControlScreen> createState() => _DroneControlScreenState();
}

class _DroneControlScreenState extends State<DroneControlScreen> {
  bool isConnected = false;
  bool autopilot = false;

@override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Container(
        width: double.infinity,
        height: double.infinity,
        decoration: BoxDecoration(
          image: DecorationImage(
            image: AssetImage('assets/sky.jpg'),
            fit: BoxFit.cover,
            colorFilter: ColorFilter.mode(
              Colors.black.withOpacity(0.5),
              BlendMode.darken,
            ),
          ),
        ),
        child: SafeArea(
          child: Stack(
            children: [
              // Main content column
              Center(
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    // Bluetooth icon + status text
                    Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        Icon(
                          Icons.bluetooth,
                          size: 36,
                          color: isConnected ? Colors.blueAccent : Colors.grey,
                        ),
                        const SizedBox(width: 12),
                        Text(
                          isConnected
                              ? 'Drone Connected!'
                              : 'Drone Disconnected!',
                          style: const TextStyle(
                              color: Colors.white,
                              fontSize: 20,
                              fontWeight: FontWeight.bold),
                        ),
                      ],
                    ),
                    const SizedBox(height: 20),

                    // Connect / Disconnect button
                    ElevatedButton(
                      onPressed: () => setState(() => isConnected = !isConnected),
                      style: ElevatedButton.styleFrom(
                        backgroundColor: Colors.green.shade700,
                        foregroundColor: Colors.white,
                        padding: const EdgeInsets.symmetric(
                            horizontal: 32, vertical: 16),
                      ),
                      child: Text(isConnected ? 'Disconnect' : 'Connect'),
                    ),
                    const SizedBox(height: 20),

                    // Autopilot button
                    ElevatedButton(
                      onPressed: isConnected
                          ? () => setState(() => autopilot = !autopilot)
                          : null,
                      style: ElevatedButton.styleFrom(
                        backgroundColor: Colors.green.shade700,
                        foregroundColor: Colors.white,
                        padding: const EdgeInsets.symmetric(
                            horizontal: 32, vertical: 16),
                      ),
                      child:
                          Text(autopilot ? 'Autopilot ON' : 'Autopilot OFF'),
                    ),

                    const SizedBox(height: 40),
                  ],
                ),
              ),

              // Back button
              Positioned(
                top: 16,
                left: 16,
                child: ElevatedButton.icon(
                  onPressed: () => Navigator.pop(context),
                  icon: const Icon(Icons.arrow_back),
                  label: const Text('Back'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.green.shade700,
                    foregroundColor: Colors.white,
                    padding: const EdgeInsets.symmetric(
                        horizontal: 16, vertical: 12),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
