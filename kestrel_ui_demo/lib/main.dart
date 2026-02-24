import 'package:flutter/material.dart';
import 'Screens/home_screen.dart';
import 'Screens/section_selection_screen.dart';
import 'Screens/image_viewer_screen.dart';
import 'Screens/drone_control_screen.dart';
import 'Screens/plant_status_screen.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Proposed Company Demo',
      initialRoute: '/',
      routes: {
        '/': (context) => const HomeScreen(),
        '/sections': (context) => const SectionSelectionScreen(),
        '/images': (context) => const ImageViewerScreen(),
        '/drone': (context) => const DroneControlScreen(),
        '/plants': (context) => const PlantStatusScreen(),
      },
    );
  }
}
