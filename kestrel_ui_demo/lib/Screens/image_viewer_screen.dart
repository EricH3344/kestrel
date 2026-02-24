import 'package:flutter/material.dart';

class ImageViewerScreen extends StatefulWidget {
  const ImageViewerScreen({super.key});

  @override
  State<ImageViewerScreen> createState() => _ImageViewerScreenState();
}

class _ImageViewerScreenState extends State<ImageViewerScreen> {
  String? selectedImage;
  final List<String> images = ['Image1.png', 'Image2.png']; // demo images

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.grey.shade800, // simple gray background
      body: SafeArea(
        child: Stack(
          children: [
            Padding(
              padding: const EdgeInsets.all(16.0),
              child: Column(
                children: [
                  const SizedBox(height: 20),

                  // Import Image button
                  ElevatedButton.icon(
                    onPressed: () {
                      // For demo: just pick first image as placeholder
                      setState(() => selectedImage = images[0]);
                    },
                    icon: const Icon(Icons.file_upload),
                    label: const Text('Import Image'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.green.shade700,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(
                          horizontal: 24, vertical: 12),
                    ),
                  ),

                  const SizedBox(height: 20),

                  // Image list table
                  Expanded(
                    child: SingleChildScrollView(
                      child: DataTable(
                        columns: const [
                          DataColumn(
                            label: Text(
                              'Images',
                              style: TextStyle(color: Colors.white),
                            ),
                          ),
                        ],
                        rows: images
                            .map(
                              (img) => DataRow(
                                cells: [
                                  DataCell(
                                    Text(
                                      img,
                                      style: const TextStyle(color: Colors.white),
                                    ),
                                    onTap: () =>
                                        setState(() => selectedImage = img),
                                  ),
                                ],
                              ),
                            )
                            .toList(),
                      ),
                    ),
                  ),

                  const SizedBox(height: 20),

                  // Displaying label
                  Text(
                    'Displaying: ${selectedImage ?? 'None'}',
                    style: const TextStyle(color: Colors.white, fontSize: 16),
                  ),
                  const SizedBox(height: 10),

                  // Image display area
                  Container(
                    width: double.infinity,
                    height: 200,
                    decoration: BoxDecoration(
                      color: Colors.grey.shade700,
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: selectedImage != null
                        ? Image.asset(
                            'assets/${selectedImage!}',
                            fit: BoxFit.contain,
                          )
                        : const Center(
                            child: Text(
                              'No image selected',
                              style: TextStyle(color: Colors.white),
                            ),
                          ),
                  ),
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
                  padding:
                      const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}