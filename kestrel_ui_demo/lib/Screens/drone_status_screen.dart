import 'package:flutter/material.dart';
import 'flight_log_screen.dart';

class DroneStatusScreen extends StatefulWidget {
  const DroneStatusScreen({super.key});

  @override
  State<DroneStatusScreen> createState() => _DroneStatusScreenState();
}

class _DroneStatusScreenState extends State<DroneStatusScreen> {

  String lastFlight = "None";
  String totalData = "0 GB";
  String storageRemaining = "128 GB";
  String? ndviImage;

  void importLatestFlight() {

    setState(() {

      lastFlight = DateTime.now().toString();
      totalData = "12.4 GB";
      storageRemaining = "115.6 GB";

      ndviImage = "assets/ndvi.jpeg";

    });

  }

  @override
  Widget build(BuildContext context) {

    return Scaffold(

      backgroundColor: Colors.grey.shade900,

      body: SafeArea(

        child: Stack(

          children: [

            Padding(
              padding: const EdgeInsets.all(20),

              child: Column(

                children: [

                  const SizedBox(height: 20),

                  const Text(
                    "Drone Status",
                    style: TextStyle(
                      color: Colors.white,
                      fontSize: 28,
                      fontWeight: FontWeight.bold,
                    ),
                  ),

                  const SizedBox(height: 30),

                  infoRow("Last Flight:", lastFlight),
                  infoRow("Total Data Size:", totalData),
                  infoRow("Storage Remaining:", storageRemaining),

                  const SizedBox(height: 30),

                  ElevatedButton.icon(
                    onPressed: importLatestFlight,
                    icon: const Icon(Icons.download),
                    label: const Text("Import Latest Flight"),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.green.shade700,
                      foregroundColor: Colors.white,
                    ),
                  ),

                  const SizedBox(height: 10),

                  ElevatedButton.icon(
                    onPressed: () {

                      Navigator.push(
                        context,
                        MaterialPageRoute(
                          builder: (context) => const FlightLogScreen(),
                        ),
                      );

                    },
                    icon: const Icon(Icons.list),
                    label: const Text("View Flights"),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.blue.shade700,
                      foregroundColor: Colors.white,
                    ),
                  ),

                  const SizedBox(height: 30),

                  const Text(
                    "Latest NDVI",
                    style: TextStyle(color: Colors.white, fontSize: 18),
                  ),

                  const SizedBox(height: 10),

                  Expanded(

                    child: Container(

                      width: double.infinity,

                      decoration: BoxDecoration(
                        color: Colors.grey.shade800,
                        borderRadius: BorderRadius.circular(12),
                      ),

                      child: ndviImage != null

                          ? Image.asset(ndviImage!)

                          : const Center(
                              child: Text(
                                "No Flight Imported",
                                style: TextStyle(color: Colors.white),
                              ),
                            ),
                    ),
                  ),
                ],
              ),
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
    );
  }

  Widget infoRow(String label, String value) {

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 5),

      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,

        children: [

          Text(label, style: const TextStyle(color: Colors.white)),

          Text(value, style: const TextStyle(color: Colors.white)),

        ],
      ),
    );
  }
}
