import cv2
import os
current_dir = os.path.dirname(os.path.abspath(__file__))
image_path = os.path.join(current_dir, "../images/Lena.png")
print("Read image from:", image_path)
image = cv2.imread(image_path)
if image is None:
    print("Failed to load image")
    exit()
cv2.imshow("Lena", image)
key = cv2.waitKey(0)
if key == ord('s'):
    save_path = os.path.join(current_dir, "../images/Lena.jpg")
    cv2.imwrite(save_path, image)
    print("Image saved to:", save_path)
else:
    print("Image not saved")

cv2.destroyAllWindows()