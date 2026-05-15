<?php
/**
 * DSM API für ESP32 Cooler Integration
 * Empfang und Abruf von Energie-Verbrauchsdaten
 * 
 * POST: Daten von ESP32 empfangen und speichern
 * GET: Daten an ESP32 zurückgeben
 */

header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST, GET');
header('Access-Control-Allow-Headers: Content-Type');

// Konfiguration
$baseDir = '/volume1/web/espcooler_data';
$sessionsDir = $baseDir . '/sessions';
$statsDir = $baseDir . '/stats';

// Verzeichnisse erstellen falls nicht vorhanden
if (!file_exists($sessionsDir)) {
    mkdir($sessionsDir, 0777, true);
}
if (!file_exists($statsDir)) {
    mkdir($statsDir, 0777, true);
}

// Request-Methode bestimmen
$method = $_SERVER['REQUEST_METHOD'];
$action = isset($_GET['action']) ? $_GET['action'] : '';

// POST: Daten empfangen und speichern
if ($method === 'POST') {
    $json = file_get_contents('php://input');
    $data = json_decode($json, true);
    
    if (!$data) {
        http_response_code(400);
        echo json_encode(['error' => 'Invalid JSON']);
        exit;
    }
    
    // Sessions speichern
    if (isset($data['sessions']) && is_array($data['sessions'])) {
        foreach ($data['sessions'] as $session) {
            $timestamp = $session['timestamp'];
            $filename = date('Ymd_His', $timestamp) . '_session.json';
            $filepath = $sessionsDir . '/' . $filename;
            file_put_contents($filepath, json_encode($session, JSON_PRETTY_PRINT));
        }
    }
    
    // Stats speichern
    if (isset($data['stats'])) {
        $date = date('Y-m-d');
        $filename = $date . '_stats.json';
        $filepath = $statsDir . '/' . $filename;
        file_put_contents($filepath, json_encode($data['stats'], JSON_PRETTY_PRINT));
    }
    
    echo json_encode(['success' => true, 'message' => 'Data saved']);
    exit;
}

// GET: Daten abrufen
if ($method === 'GET') {
    if ($action === 'list' || $action === '') {
        // Alle Sessions zurückgeben
        $sessions = [];
        $files = glob($sessionsDir . '/*_session.json');
        rsort($files); // Neueste zuerst
        
        foreach ($files as $file) {
            $json = file_get_contents($file);
            $sessions[] = json_decode($json, true);
        }
        
        echo json_encode(['sessions' => $sessions]);
        exit;
    }
    
    if ($action === 'stats') {
        // Alle Stats zurückgeben
        $stats = [];
        $files = glob($statsDir . '/*_stats.json');
        rsort($files); // Neueste zuerst
        
        foreach ($files as $file) {
            $json = file_get_contents($file);
            $stats[] = json_decode($json, true);
        }
        
        echo json_encode(['stats' => $stats]);
        exit;
    }
    
    if ($action === 'all') {
        // Sessions + Stats zurückgeben
        $sessions = [];
        $files = glob($sessionsDir . '/*_session.json');
        rsort($files);
        foreach ($files as $file) {
            $json = file_get_contents($file);
            $sessions[] = json_decode($json, true);
        }
        
        $stats = [];
        $files = glob($statsDir . '/*_stats.json');
        rsort($files);
        foreach ($files as $file) {
            $json = file_get_contents($file);
            $stats[] = json_decode($json, true);
        }
        
        echo json_encode(['sessions' => $sessions, 'stats' => $stats]);
        exit;
    }
}

// Ungültige Anfrage
http_response_code(405);
echo json_encode(['error' => 'Method not allowed']);
