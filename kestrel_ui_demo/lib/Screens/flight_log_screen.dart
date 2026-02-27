import 'package:flutter/material.dart';

class FlightLogScreen extends StatelessWidget {
  const FlightLogScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final flights = [
      "Flight 001 - 2026-02-26",
      "Flight 002 - 2026-02-20",
      "Flight 003 - 2026-02-15",
    ];

    return Scaffold(
      body: Container(
        width: double.infinity,
        height: double.infinity, // Important! fills the screen
        decoration: BoxDecoration(
          image: DecorationImage(
            image: AssetImage('assets/drone.jpg'), // your background image
            fit: BoxFit.cover,
            colorFilter: ColorFilter.mode(
              Colors.black.withOpacity(0.5), // dark overlay
              BlendMode.darken,
            ),
          ),
        ),
        child: SafeArea(
          child: Stack(
            children: [
              Column(
                children: [
                  const SizedBox(height: 20),

                  const Text(
                    "Flight Logs",
                    style: TextStyle(
                      color: Colors.white,
                      fontSize: 28,
                      fontWeight: FontWeight.bold,
                    ),
                  ),

                  Expanded(
                    child: ListView.builder(
                      itemCount: flights.length,

                      itemBuilder: (context, index) {
                        return ListTile(
                          title: Text(
                            flights[index],
                            style: const TextStyle(color: Colors.white),
                          ),

                          leading: const Icon(
                            Icons.flight,
                            color: Colors.white,
                          ),
                        );
                      },
                    ),
                  ),
                ],
              ),

              Positioned(
                top: 10,
                left: 10,
                child: ElevatedButton.icon(
                  onPressed: () => Navigator.pop(context),
                  icon: const Icon(Icons.arrow_back),
                  label: const Text("Back"),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
