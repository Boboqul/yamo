<?php

session_start();

require __DIR__ . '/../config/functions.php';

header('Content-Type: application/json; charset=utf-8');

$email = isset($_POST['email']) ? trim($_POST['email']) : '';
$password = isset($_POST['password']) ? $_POST['password'] : '';

if ($email === '' || $password === '') {
    echo json_encode(array(
        'ok' => false,
        'error' => 'Email va parol kerak.'
    ), JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

try {
    $pdo = db();

    $stmt = $pdo->prepare(
        'SELECT id, full_name, email, password_hash, mail_password, email_verified
        FROM users
        WHERE email = ?
        LIMIT 1'
    );

    $stmt->execute(array($email));

    $user = $stmt->fetch(PDO::FETCH_ASSOC);

    if (!$user) {
        echo json_encode(array(
            'ok' => false,
            'error' => 'Email yoki parol noto‘g‘ri.'
        ), JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
        exit;
    }

    if (!password_verify($password, $user['password_hash'])) {
        echo json_encode(array(
            'ok' => false,
            'error' => 'Email yoki parol noto‘g‘ri.'
        ), JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
        exit;
    }

    if ((int)$user['email_verified'] !== 1) {
        echo json_encode(array(
            'ok' => false,
            'error' => 'Avval emailingizni tasdiqlang.'
        ), JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
        exit;
    }

    /*
     * PHP sessiyasi
     */
    $_SESSION['user_id'] = (int)$user['id'];
    $_SESSION['mail_user'] = $user['email'];
    $_SESSION['mail_password'] = decrypt_mail_password($user['mail_password']);
    $_SESSION['email_verified'] = 1;

    echo json_encode(array(
        'ok' => true,
        'user_id' => (int)$user['id'],
        'full_name' => $user['full_name'],
        'email' => $user['email']
    ), JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);

} catch (Exception $e) {

    echo json_encode(array(
        'ok' => false,
        'error' => 'Server xatosi: ' . $e->getMessage()
    ), JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
}