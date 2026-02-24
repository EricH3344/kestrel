import 'package:flutter/material.dart';

class PlantStatusScreen extends StatefulWidget {
  const PlantStatusScreen({super.key});

  @override
  State<PlantStatusScreen> createState() => _PlantStatusScreenState();
}

class _PlantStatusScreenState extends State<PlantStatusScreen> {
  String? selectedPlant;

  final Map<String, Map<String, String>> plantData = {
    'Plant A': {
      'Height': '35 cm',
      'Growth Rate': '1.2 cm/day',
      'Liveliness': 'High',
      'GMO Modifier': '0.85',
    },
    'Plant B': {
      'Height': '22 cm',
      'Growth Rate': '0.8 cm/day',
      'Liveliness': 'Moderate',
      'GMO Modifier': '0.60',
    },
  };

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Container(
        width: double.infinity,
        height: double.infinity, // Important! fills the screen
        decoration: BoxDecoration(
          image: DecorationImage(
            image: AssetImage('assets/plants.jpg'), // your background image
            fit: BoxFit.cover,
            colorFilter: ColorFilter.mode(
              Colors.black.withOpacity(0.5), // dark overlay
              BlendMode.darken,
            ),
          ),
        ),
        child: SafeArea(
          child: Padding(
            padding: const EdgeInsets.all(16.0),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // Left: Plant list
                Expanded(
                  flex: 1,
                  child: SingleChildScrollView(
                    child: DataTable(
                      columns: const [
                        DataColumn(
                          label: Text(
                            'Plants',
                            style: TextStyle(color: Colors.white),
                          ),
                        ),
                      ],
                      rows: plantData.keys
                          .map(
                            (plant) => DataRow(
                              cells: [
                                DataCell(
                                  Text(
                                    plant,
                                    style: const TextStyle(color: Colors.white),
                                  ),
                                  onTap: () =>
                                      setState(() => selectedPlant = plant),
                                ),
                              ],
                            ),
                          )
                          .toList(),
                    ),
                  ),
                ),

                const SizedBox(width: 24),

                // Right: Selected plant metrics
                Expanded(
                  flex: 2,
                  child: selectedPlant == null
                      ? const Center(
                          child: Text(
                            'Select a plant',
                            style: TextStyle(color: Colors.white, fontSize: 16),
                          ),
                        )
                      : Container(
                          padding: const EdgeInsets.all(16),
                          decoration: BoxDecoration(
                            color: Colors.white.withOpacity(
                              0.1,
                            ), // optional subtle panel
                            borderRadius: BorderRadius.circular(12),
                          ),
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: plantData[selectedPlant!]!.entries
                                .map(
                                  (e) => Padding(
                                    padding: const EdgeInsets.symmetric(
                                      vertical: 4,
                                    ),
                                    child: Text(
                                      '${e.key}: ${e.value}',
                                      style: const TextStyle(
                                        fontSize: 16,
                                        color: Colors.white,
                                      ),
                                    ),
                                  ),
                                )
                                .toList(),
                          ),
                        ),
                ),
                Positioned(
                  top: 16,
                  left: 16,
                  child: ElevatedButton.icon(
                    onPressed: () =>
                        Navigator.pop(context), // go back to Section Selection
                    icon: const Icon(Icons.arrow_back),
                    label: const Text('Back'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.green.shade700,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(
                        horizontal: 16,
                        vertical: 12,
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
