import 'package:flutter/material.dart';

class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
  body: Container(
    width: double.infinity,
    height: double.infinity,
    decoration: BoxDecoration(
      image: DecorationImage(
        image: AssetImage('assets/test_image.jpg'),
        fit: BoxFit.cover,
        colorFilter: ColorFilter.mode(
      Colors.black.withOpacity(0.4),
        BlendMode.darken,
    ),
      ),
    ),
  child: SafeArea(
    child: Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Image.asset('assets/kestrel.png', width: 200),
          const SizedBox(height: 20),
          const Text(
            'Kestrel',
            style: TextStyle(
              fontSize: 28,
              fontWeight: FontWeight.bold,
              color: Colors.white,
            ),
          ),
          const SizedBox(height: 40),
          ElevatedButton(
            onPressed: () =>
                Navigator.pushNamed(context, '/sections'),
            child: const Text('Enter Application'),
              style: ElevatedButton.styleFrom(
              foregroundColor: Colors.white,
              backgroundColor: Colors.green.shade700,
              padding: const EdgeInsets.symmetric(horizontal: 32, vertical: 16),
              ),
          )
        ],
      ),
    ),
  ),
  ),
);
  }
}
