<?php

header('Content-Type: application/json; charset=utf-8');

session_start();

require_once __DIR__ . '/../config/functions.php';

function api_error($message, $code = 400)
{
    http_response_code($code);

    echo json_encode(
        array(
            'ok' => false,
            'error' => $message
        ),
        JSON_UNESCAPED_UNICODE
    );

    exit;
}

function get_token()
{
    $headers = array();

    if (function_exists('getallheaders')) {
        $headers = getallheaders();
    }

    if (isset($_SERVER['HTTP_AUTHORIZATION'])) {
        return $_SERVER['HTTP_AUTHORIZATION'];
    }

    if (isset($_SERVER['REDIRECT_HTTP_AUTHORIZATION'])) {
        return $_SERVER['REDIRECT_HTTP_AUTHORIZATION'];
    }

    if (isset($headers['Authorization'])) {
        return $headers['Authorization'];
    }

    if (isset($headers['authorization'])) {
        return $headers['authorization'];
    }

    return '';
}

$auth = get_token();

if (strpos($auth, 'Bearer ') !== 0) {
    api_error('Token kerak.', 401);
}

$token = trim(substr($auth, 7));

if ($token === '') {
    api_error('Token bo‘sh.', 401);
}

/*
 * Временно используем тот же механизм,
 * который уже работает в api/sent.php.
 */

$pdo = db();

$stmt = $pdo->prepare("
    SELECT id, email, mail_password, email_verified
    FROM users
    WHERE api_token = ?
    LIMIT 1
");

$stmt->execute(array($token));

$user = $stmt->fetch(PDO::FETCH_ASSOC);

if (!$user) {
    api_error('Token noto‘g‘ri.', 401);
}

if ((int)$user['email_verified'] !== 1) {
    api_error('Email tasdiqlanmagan.', 403);
}

$uid = isset($_GET['uid']) ? (int)$_GET['uid'] : 0;

if ($uid <= 0) {
    api_error('UID kerak.');
}

$mailUser = $user['email'];
$mailPassword = decrypt_mail_password($user['mail_password']);

if ($mailPassword === '') {
    api_error('Mail parolini ochib bo‘lmadi.', 500);
}

$folder = find_folder('INBOX.Sent');

if (!$folder) {
    api_error('INBOX.Sent topilmadi.', 404);
}

$imap = imap_connect($mailUser, $mailPassword, $folder);

if (!$imap) {
    api_error('IMAP ulanish xatosi.', 500);
}

$messages = imap_search(
    $imap,
    'UID ' . $uid,
    SE_UID
);

if (!$messages || !in_array($uid, $messages)) {
    imap_close($imap);
    api_error('Xat topilmadi.', 404);
}

$overview = imap_fetch_overview($imap, $uid, FT_UID);

if (!$overview || !isset($overview[0])) {
    imap_close($imap);
    api_error('Xat ma’lumotlarini olishda xato.', 500);
}

$ov = $overview[0];

$structure = imap_fetchstructure($imap, $uid, FT_UID);

if (!$structure) {
    imap_close($imap);
    api_error('Xat tuzilmasini olishda xato.', 500);
}

$body = '';
$attachments = array();

if (function_exists('get_mail_part')) {

    $plain = get_mail_part(
        $imap,
        $uid,
        $structure,
        'TEXT/PLAIN'
    );

    if ($plain !== '') {
        $body = $plain;
    } else {

        $html = get_mail_part(
            $imap,
            $uid,
            $structure,
            'TEXT/HTML'
        );

        $body = $html;
    }
}

if (function_exists('get_attachments')) {
    $attachments = get_attachments(
        $structure,
        ''
    );
}

imap_close($imap);

echo json_encode(
    array(
        'ok' => true,
        'folder' => 'INBOX.Sent',
        'message' => array(
            'uid' => $uid,
            'to' => isset($ov->to) ? decode_header_text($ov->to) : '',
            'subject' => isset($ov->subject) ? decode_header_text($ov->subject) : '',
            'date' => isset($ov->date) ? $ov->date : '',
            'body' => $body,
            'attachments' => $attachments
        )
    ),
    JSON_UNESCAPED_UNICODE
);