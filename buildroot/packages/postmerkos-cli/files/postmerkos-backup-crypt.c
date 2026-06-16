#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_INPUT_SIZE (4U * 1024U * 1024U)
#define PBKDF2_ITERATIONS 100000U
#define SALT_SIZE 8U
#define KEY_SIZE 32U
#define IV_SIZE 16U
#define DERIVED_SIZE (KEY_SIZE + IV_SIZE)

static const unsigned char openssl_header[8] = {
  'S', 'a', 'l', 't', 'e', 'd', '_', '_'
};

static void secure_zero(void *pointer, size_t length) {
  volatile unsigned char *cursor = pointer;
  while (length--) *cursor++ = 0;
}

static int read_password(char *password, size_t size) {
  if (!fgets(password, (int)size, stdin)) {
    fprintf(stderr, "unable to read backup password\n");
    return -1;
  }
  size_t length = strcspn(password, "\r\n");
  password[length] = '\0';
  if (length == 0 || length > 128) {
    fprintf(stderr, "backup password must be 1-128 characters\n");
    return -1;
  }
  return (int)length;
}

static int read_exact_random(unsigned char *buffer, size_t length) {
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) return -1;
  size_t offset = 0;
  while (offset < length) {
    ssize_t got = read(fd, buffer + offset, length - offset);
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) { close(fd); return -1; }
    offset += (size_t)got;
  }
  close(fd);
  return 0;
}

static int read_file(const char *path, unsigned char **contents, size_t *length) {
  struct stat st;
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
      (uint64_t)st.st_size > MAX_INPUT_SIZE + 32U) {
    fprintf(stderr, "invalid or oversized input file\n");
    return -1;
  }
  FILE *file = fopen(path, "rb");
  if (!file) { perror(path); return -1; }
  size_t size = (size_t)st.st_size;
  unsigned char *buffer = malloc(size ? size : 1U);
  if (!buffer) { fclose(file); fprintf(stderr, "out of memory\n"); return -1; }
  if (size && fread(buffer, 1, size, file) != size) {
    perror("read"); free(buffer); fclose(file); return -1;
  }
  if (fclose(file) != 0) { perror("close"); free(buffer); return -1; }
  *contents = buffer;
  *length = size;
  return 0;
}

static int write_file(const char *path, const unsigned char *contents,
                      size_t length) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) { perror(path); return -1; }
  size_t offset = 0;
  while (offset < length) {
    ssize_t written = write(fd, contents + offset, length - offset);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) { perror("write"); close(fd); unlink(path); return -1; }
    offset += (size_t)written;
  }
  int sync_rc = fsync(fd);
  int close_rc = close(fd);
  if (sync_rc != 0 || close_rc != 0) {
    perror(sync_rc != 0 ? "fsync" : "close");
    unlink(path);
    return -1;
  }
  return 0;
}

static int derive(const char *password, size_t password_length,
                  const unsigned char salt[SALT_SIZE],
                  unsigned char output[DERIVED_SIZE]) {
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return -1;
  mbedtls_md_context_t context;
  mbedtls_md_init(&context);
  int rc = mbedtls_md_setup(&context, info, 1);
  if (rc == 0) {
    rc = mbedtls_pkcs5_pbkdf2_hmac(&context,
        (const unsigned char *)password, password_length,
        salt, SALT_SIZE, PBKDF2_ITERATIONS, DERIVED_SIZE, output);
  }
  mbedtls_md_free(&context);
  return rc;
}

static int encrypt_file(const char *input_path, const char *output_path,
                        const char *password, size_t password_length) {
  unsigned char *plain = NULL;
  size_t plain_length = 0;
  if (read_file(input_path, &plain, &plain_length) != 0) return -1;
  if (plain_length > MAX_INPUT_SIZE) {
    fprintf(stderr, "configuration input exceeds 4 MiB\n");
    free(plain); return -1;
  }

  size_t padded_length = ((plain_length / 16U) + 1U) * 16U;
  unsigned char *padded = calloc(1, padded_length);
  unsigned char *output = malloc(16U + padded_length);
  if (!padded || !output) {
    fprintf(stderr, "out of memory\n");
    free(plain); free(padded); free(output); return -1;
  }
  memcpy(padded, plain, plain_length);
  unsigned char padding = (unsigned char)(padded_length - plain_length);
  memset(padded + plain_length, padding, padding);

  unsigned char salt[SALT_SIZE];
  unsigned char derived[DERIVED_SIZE];
  if (read_exact_random(salt, sizeof(salt)) != 0 ||
      derive(password, password_length, salt, derived) != 0) {
    fprintf(stderr, "key derivation failed\n");
    secure_zero(derived, sizeof(derived));
    free(plain); free(padded); free(output); return -1;
  }
  memcpy(output, openssl_header, 8U);
  memcpy(output + 8U, salt, SALT_SIZE);

  unsigned char iv[IV_SIZE];
  memcpy(iv, derived + KEY_SIZE, sizeof(iv));
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  int rc = mbedtls_aes_setkey_enc(&aes, derived, 256U);
  if (rc == 0) rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT,
      padded_length, iv, padded, output + 16U);
  mbedtls_aes_free(&aes);
  if (rc == 0) rc = write_file(output_path, output, 16U + padded_length);
  else fprintf(stderr, "AES encryption failed\n");

  secure_zero(derived, sizeof(derived));
  secure_zero(iv, sizeof(iv));
  secure_zero(padded, padded_length);
  secure_zero(plain, plain_length);
  free(plain); free(padded); free(output);
  return rc;
}

static int decrypt_file(const char *input_path, const char *output_path,
                        const char *password, size_t password_length) {
  unsigned char *input = NULL;
  size_t input_length = 0;
  if (read_file(input_path, &input, &input_length) != 0) return -1;
  if (input_length < 32U || memcmp(input, openssl_header, 8U) != 0 ||
      ((input_length - 16U) % 16U) != 0) {
    fprintf(stderr, "input is not a supported encrypted backup\n");
    free(input); return -1;
  }

  size_t cipher_length = input_length - 16U;
  unsigned char *plain = malloc(cipher_length);
  unsigned char derived[DERIVED_SIZE];
  if (!plain || derive(password, password_length, input + 8U, derived) != 0) {
    fprintf(stderr, "key derivation failed\n");
    free(input); free(plain); secure_zero(derived, sizeof(derived)); return -1;
  }

  unsigned char iv[IV_SIZE];
  memcpy(iv, derived + KEY_SIZE, sizeof(iv));
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  int rc = mbedtls_aes_setkey_dec(&aes, derived, 256U);
  if (rc == 0) rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT,
      cipher_length, iv, input + 16U, plain);
  mbedtls_aes_free(&aes);

  size_t plain_length = 0;
  if (rc == 0) {
    unsigned int padding = plain[cipher_length - 1U];
    if (padding == 0 || padding > 16U || padding > cipher_length) rc = -1;
    else {
      for (size_t i = 0; i < padding; i++)
        if (plain[cipher_length - 1U - i] != padding) rc = -1;
      if (rc == 0) plain_length = cipher_length - padding;
    }
  }
  if (rc == 0) rc = write_file(output_path, plain, plain_length);
  else fprintf(stderr, "incorrect password or damaged encrypted backup\n");

  secure_zero(derived, sizeof(derived));
  secure_zero(iv, sizeof(iv));
  secure_zero(plain, cipher_length);
  secure_zero(input, input_length);
  free(input); free(plain);
  return rc;
}

static void usage(const char *program) {
  fprintf(stderr, "usage: %s encrypt|decrypt INPUT OUTPUT\n", program);
}

int main(int argc, char **argv) {
  if (argc != 4 || (strcmp(argv[1], "encrypt") && strcmp(argv[1], "decrypt"))) {
    usage(argv[0]);
    return 2;
  }
  char password[130];
  int password_length = read_password(password, sizeof(password));
  if (password_length < 0) return 2;
  int rc = !strcmp(argv[1], "encrypt")
      ? encrypt_file(argv[2], argv[3], password, (size_t)password_length)
      : decrypt_file(argv[2], argv[3], password, (size_t)password_length);
  secure_zero(password, sizeof(password));
  return rc == 0 ? 0 : 1;
}
