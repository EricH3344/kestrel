import 'package:flutter/material.dart';

class SectionSelectionScreen extends StatelessWidget {
  const SectionSelectionScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
  body: Container(
    decoration: BoxDecoration(
      image: DecorationImage(
        image: AssetImage('assets/farm_land.jpg'),
        fit: BoxFit.cover,
        colorFilter: ColorFilter.mode(
          Colors.black.withOpacity(0.5),
          BlendMode.darken,
        ),
      ),
    ),
    child: SafeArea(
      child: Center(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            const SizedBox(height: 60),

            const Text(
              "Select a Section",
              style: TextStyle(
                fontSize: 28,
                fontWeight: FontWeight.bold,
                color: Colors.white,
              ),
            ),

 
            ElevatedButton(
              onPressed: () => Navigator.pushNamed(context, '/images'),
              style: ElevatedButton.styleFrom(
                foregroundColor: Colors.white,
                backgroundColor: Colors.green.shade700,
                padding: const EdgeInsets.symmetric(horizontal: 32, vertical: 16),
              ),
              child: const Text("Image Viewer"),
            ),
            const SizedBox(height: 20),

            ElevatedButton(
              onPressed: () => Navigator.pushNamed(context, '/drone'),
              style: ElevatedButton.styleFrom(
                foregroundColor: Colors.white,
                backgroundColor: Colors.green.shade700,
                padding: const EdgeInsets.symmetric(horizontal: 32, vertical: 16),
              ),
              child: const Text("Drone Control"),
            ),
            const SizedBox(height: 20),

            ElevatedButton(
              onPressed: () => Navigator.pushNamed(context, '/plants'),
              style: ElevatedButton.styleFrom(
                foregroundColor: Colors.white,
                backgroundColor: Colors.green.shade700,
                padding: const EdgeInsets.symmetric(horizontal: 32, vertical: 16),
              ),
              child: const Text("Plant Status"),
            ),

            const SizedBox(height: 60),

            OutlinedButton(
              onPressed: () =>
                  Navigator.popUntil(context, ModalRoute.withName('/')),
              style: OutlinedButton.styleFrom(
                foregroundColor: Colors.white,
                side: const BorderSide(color: Colors.white),
                padding: const EdgeInsets.symmetric(horizontal: 32, vertical: 16),
              ),
              child: const Text("Back to Home"),
            ),
          ],
        ),
      ),
    ),
  ),
);
  }
}
